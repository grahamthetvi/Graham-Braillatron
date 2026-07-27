#include "library_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace braillatron::connect {

namespace fs = std::filesystem;

namespace {

constexpr const char *kUserAgent = "Braillatron/1.0 (accessibility device)";

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string url_encode(const std::string &value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else if (ch == ' ') {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string find_json_object(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    if (start >= json.size() || json[start] != '{') {
        return {};
    }
    int depth = 0;
    for (size_t i = start; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

std::string extract_array_body(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":[";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + needle.size();
    int depth = 1;
    bool in_string = false;
    bool escape = false;
    for (size_t i = start; i < json.size(); ++i) {
        const char ch = json[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(start, i - start);
            }
        }
    }
    return {};
}

std::string extract_string_array_first(const std::string &json, const std::string &key)
{
    const std::string body = extract_array_body(json, key);
    if (body.empty()) {
        return {};
    }
    const auto items = json_split_objects("[" + body + "]");
    if (!items.empty()) {
        return json_get_string(items.front(), "name");
    }
    size_t start = body.find('"');
    if (start == std::string::npos) {
        return {};
    }
    ++start;
    const size_t end = body.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return body.substr(start, end - start);
}

std::string author_from_gutendex(const std::string &result)
{
    const std::string authors_body = extract_array_body(result, "authors");
    if (authors_body.empty()) {
        return "Unknown";
    }
    const auto objects = json_split_objects("[" + authors_body + "]");
    if (objects.empty()) {
        return "Unknown";
    }
    const std::string name = json_get_string(objects.front(), "name");
    return name.empty() ? "Unknown" : name;
}

std::string pick_download_url(const std::string &result, const std::string &preferred_format)
{
    const std::string formats = find_json_object(result, "formats");
    if (formats.empty()) {
        return {};
    }
    if (preferred_format == "epub") {
        const std::string epub = json_get_string(formats, "application/epub+zip");
        if (!epub.empty()) {
            return epub;
        }
    }
    const std::string txt = json_get_string(formats, "text/plain; charset=utf-8");
    if (!txt.empty()) {
        return txt;
    }
    const std::string txt_us = json_get_string(formats, "text/plain; charset=us-ascii");
    if (!txt_us.empty()) {
        return txt_us;
    }
    return json_get_string(formats, "application/epub+zip");
}

std::string format_from_url(const std::string &url)
{
    if (url.find(".epub") != std::string::npos) {
        return "epub";
    }
    if (url.find(".m4b") != std::string::npos) {
        return "m4b";
    }
    if (url.find(".mp3") != std::string::npos || url.find("_mp3") != std::string::npos) {
        return "mp3";
    }
    if (url.find(".zip") != std::string::npos) {
        return "zip";
    }
    if (url.find(".pdf") != std::string::npos) {
        return "pdf";
    }
    return "txt";
}

int json_get_int_value(const std::string &json, const std::string &key, int default_value)
{
    const std::string value = json_get_string(json, key);
    if (value.empty()) {
        return default_value;
    }
    return std::atoi(value.c_str());
}

std::string sanitize_filename(const std::string &value)
{
    std::string out;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
            out += ch;
        } else if (ch == ' ') {
            out += '_';
        }
    }
    if (out.empty()) {
        return "book";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

std::string work_key_from_id(const std::string &result_id)
{
    if (result_id.find("/works/") != std::string::npos) {
        const size_t pos = result_id.rfind('/');
        return result_id.substr(pos + 1);
    }
    return result_id;
}

std::string pick_archive_file(const std::string &metadata_json, const std::string &preferred)
{
    const std::string files_body = extract_array_body(metadata_json, "files");
    if (files_body.empty()) {
        return {};
    }
    const auto files = json_split_objects("[" + files_body + "]");
    std::string epub;
    std::string pdf;
    std::string txt;
    std::string m4b;
    std::string mp3_zip;
    std::string zip;
    for (const std::string &file : files) {
        const std::string name = json_get_string(file, "name");
        if (name.empty()) {
            continue;
        }
        const std::string lower = name;
        if (name.find(".epub") != std::string::npos) {
            epub = name;
        } else if (name.find(".pdf") != std::string::npos && pdf.empty()) {
            pdf = name;
        } else if (name.find(".txt") != std::string::npos && txt.empty()) {
            txt = name;
        } else if (name.find(".m4b") != std::string::npos) {
            m4b = name;
        } else if (name.find("_64kb_mp3.zip") != std::string::npos ||
                   name.find("_mp3.zip") != std::string::npos) {
            mp3_zip = name;
        } else if (name.find(".zip") != std::string::npos && zip.empty()) {
            zip = name;
        }
        (void)lower;
    }
    if (preferred == "audio") {
        if (!m4b.empty()) {
            return m4b;
        }
        if (!mp3_zip.empty()) {
            return mp3_zip;
        }
        if (!zip.empty()) {
            return zip;
        }
    }
    if (!epub.empty()) {
        return epub;
    }
    if (!pdf.empty()) {
        return pdf;
    }
    if (!txt.empty()) {
        return txt;
    }
    if (!m4b.empty()) {
        return m4b;
    }
    if (!mp3_zip.empty()) {
        return mp3_zip;
    }
    return zip;
}

std::vector<std::string> archive_docs_from_response(const std::string &response, uint32_t limit)
{
    std::string docs_body = extract_array_body(response, "docs");
    if (docs_body.empty()) {
        const std::string response_obj = find_json_object(response, "response");
        if (!response_obj.empty()) {
            docs_body = extract_array_body(response_obj, "docs");
        }
    }
    if (docs_body.empty()) {
        return {};
    }
    auto docs = json_split_objects("[" + docs_body + "]");
    if (docs.size() > limit) {
        docs.resize(limit);
    }
    return docs;
}

std::string creator_from_archive_doc(const std::string &doc)
{
    const std::string direct = json_get_string(doc, "creator");
    if (!direct.empty() && direct.front() != '[') {
        return direct;
    }
    const std::string creator = extract_string_array_first(doc, "creator");
    return creator.empty() ? "Unknown" : creator;
}

} // namespace

LibraryBackend::LibraryBackend(LibraryConfig config)
    : config_(std::move(config))
{
}

std::string LibraryBackend::curl_fetch(const std::string &url, bool archive_api) const
{
    std::string cmd = "curl -fsSL --max-time 30 -A '" + std::string(kUserAgent) + "'";
    if (archive_api && !config_.archive_contact_email.empty()) {
        cmd += " -H 'Authorization: LOW " + config_.archive_contact_email + ":request'";
    }
    cmd += " '" + url + "' 2>/dev/null";
    return run_command(cmd);
}

std::string LibraryBackend::download_file(const std::string &url, const std::string &dest) const
{
    ensure_directory(fs::path(dest).parent_path().string());
    const std::string part = dest + ".part";
    std::string cmd = "curl -fsSL --max-time 300 -A '" + std::string(kUserAgent) + "'";
    if (url.find("archive.org") != std::string::npos &&
        !config_.archive_contact_email.empty()) {
        cmd += " -H 'Authorization: LOW " + config_.archive_contact_email + ":request'";
    }
    cmd += " -o '" + part + "' '" + url + "' 2>/dev/null";
    if (run_command_status(cmd) != 0) {
        return "{\"ok\":false,\"error\":\"download failed\"}";
    }
    if (!atomic_move_file(part, dest)) {
        return "{\"ok\":false,\"error\":\"save failed\"}";
    }
    return "{\"ok\":true,\"path\":\"" + json_escape(dest) + "\"}";
}

bool LibraryBackend::register_downloaded_book(const std::string &source,
                                              const std::string &external_id,
                                              const std::string &title,
                                              const std::string &author,
                                              const std::string &local_path,
                                              const std::string &format, int gutenberg_id)
{
    ensure_directory(fs::path(config_.catalog_path).parent_path().string());

    std::vector<std::string> entries;
    if (file_exists(config_.catalog_path)) {
        std::ifstream in(config_.catalog_path);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string json = buffer.str();
        for (const auto &obj : json_split_objects("[" + extract_array_body(json, "books") + "]")) {
            const std::string existing_source = json_get_string(obj, "source");
            const std::string existing_id = json_get_string(obj, "external_id");
            if (existing_source == source && existing_id == external_id) {
                return true;
            }
            if (gutenberg_id > 0 &&
                json_get_int_value(obj, "gutenberg_id", 0) == gutenberg_id) {
                return true;
            }
            entries.push_back(obj);
        }
    }

    std::string next_id = source + "-" + sanitize_filename(external_id);
    if (next_id.size() > 48) {
        next_id.resize(48);
    }
    std::ostringstream book;
    book << "{\n"
         << "      \"id\":\"" << json_escape(next_id) << "\",\n"
         << "      \"title\":\"" << json_escape(title) << "\",\n"
         << "      \"author\":\"" << json_escape(author) << "\",\n"
         << "      \"format\":\"" << json_escape(format) << "\",\n"
         << "      \"local_path\":\"" << json_escape(local_path) << "\",\n"
         << "      \"source\":\"" << json_escape(source) << "\",\n"
         << "      \"external_id\":\"" << json_escape(external_id) << "\"";
    if (gutenberg_id > 0) {
        book << ",\n      \"gutenberg_id\":" << gutenberg_id;
    }
    book << "\n    }";
    entries.push_back(book.str());

    const std::string temp_path = config_.catalog_path + ".tmp";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }
    out << "{\n  \"books\":[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        out << "    " << entries[i];
    }
    out << "\n  ]\n}\n";
    out.flush();
    if (!out.good()) {
        return false;
    }
    return rename(temp_path.c_str(), config_.catalog_path.c_str()) == 0;
}

std::string LibraryBackend::search(const std::string &query, const std::string &source)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"library disabled\"}";
    }
    if (trim(query).empty()) {
        return "{\"ok\":false,\"error\":\"empty query\"}";
    }

    const std::string normalized = source.empty() ? "gutendex" : source;
    if (normalized == "gutendex" || normalized == "gutenberg") {
        return search_gutendex(query);
    }
    if (normalized == "openlibrary") {
        return search_openlibrary(query);
    }
    if (normalized == "archive" || normalized == "internet_archive") {
        return search_archive(query);
    }
    if (normalized == "librivox") {
        return search_librivox(query);
    }
    return "{\"ok\":false,\"error\":\"unknown source\"}";
}

std::string LibraryBackend::download(const std::string &source, const std::string &result_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"library disabled\"}";
    }
    if (trim(result_id).empty()) {
        return "{\"ok\":false,\"error\":\"invalid id\"}";
    }

    const std::string normalized = source.empty() ? "gutendex" : source;
    if (normalized == "gutendex" || normalized == "gutenberg") {
        return download_gutendex(result_id);
    }
    if (normalized == "openlibrary") {
        return download_openlibrary(result_id);
    }
    if (normalized == "archive" || normalized == "internet_archive") {
        return download_archive(result_id);
    }
    if (normalized == "librivox") {
        return download_librivox(result_id);
    }
    return "{\"ok\":false,\"error\":\"unknown source\"}";
}

std::string LibraryBackend::search_gutendex(const std::string &query)
{
    const std::string url = config_.gutendex_url + "/?search=" + url_encode(query);
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const std::string results_body = extract_array_body(response, "results");
    if (results_body.empty()) {
        return "{\"ok\":true,\"source\":\"gutendex\",\"results\":[]}";
    }

    const auto objects = json_split_objects("[" + results_body + "]");
    std::ostringstream out;
    out << "{\"ok\":true,\"source\":\"gutendex\",\"results\":[";
    uint32_t count = 0;
    for (const std::string &result : objects) {
        if (count >= config_.search_limit) {
            break;
        }
        const int id = json_get_int_value(result, "id", 0);
        const std::string title = json_get_string(result, "title");
        if (id <= 0 || title.empty()) {
            continue;
        }
        if (count > 0) {
            out << ',';
        }
        out << "{\"id\":\"" << id << "\",\"title\":\"" << json_escape(title) << "\",\"author\":\""
            << json_escape(author_from_gutendex(result)) << "\",\"source\":\"gutendex\"}";
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string LibraryBackend::search_openlibrary(const std::string &query)
{
    const std::string url = config_.openlibrary_url + "/search.json?q=" + url_encode(query) +
                            "&limit=" + std::to_string(config_.search_limit) +
                            "&fields=key,title,author_name,ia";
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const std::string results_body = extract_array_body(response, "docs");
    if (results_body.empty()) {
        return "{\"ok\":true,\"source\":\"openlibrary\",\"results\":[]}";
    }

    const auto objects = json_split_objects("[" + results_body + "]");
    std::ostringstream out;
    out << "{\"ok\":true,\"source\":\"openlibrary\",\"results\":[";
    uint32_t count = 0;
    for (const std::string &result : objects) {
        if (count >= config_.search_limit) {
            break;
        }
        const std::string key = json_get_string(result, "key");
        const std::string title = json_get_string(result, "title");
        if (key.empty() || title.empty()) {
            continue;
        }
        const std::string author = extract_string_array_first(result, "author_name");
        const std::string ia = extract_string_array_first(result, "ia");
        if (count > 0) {
            out << ',';
        }
        out << "{\"id\":\"" << json_escape(key) << "\",\"title\":\"" << json_escape(title)
            << "\",\"author\":\"" << json_escape(author.empty() ? "Unknown" : author)
            << "\",\"source\":\"openlibrary\"";
        if (!ia.empty()) {
            out << ",\"ia\":\"" << json_escape(ia) << "\"";
        }
        out << '}';
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string LibraryBackend::search_archive(const std::string &query)
{
    const std::string q = url_encode(query) + "%20AND%20mediatype:texts";
    const std::string url = config_.archive_search_url + "?q=" + q +
                            "&fl[]=identifier,title,creator&rows=" +
                            std::to_string(config_.search_limit) + "&page=1&output=json";
    const std::string response = curl_fetch(url, true);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const auto docs = archive_docs_from_response(response, config_.search_limit);
    std::ostringstream out;
    out << "{\"ok\":true,\"source\":\"archive\",\"results\":[";
    uint32_t count = 0;
    for (const std::string &doc : docs) {
        const std::string identifier = json_get_string(doc, "identifier");
        const std::string title = json_get_string(doc, "title");
        if (identifier.empty() || title.empty()) {
            continue;
        }
        if (count > 0) {
            out << ',';
        }
        out << "{\"id\":\"" << json_escape(identifier) << "\",\"title\":\"" << json_escape(title)
            << "\",\"author\":\"" << json_escape(creator_from_archive_doc(doc))
            << "\",\"source\":\"archive\"}";
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string LibraryBackend::search_librivox(const std::string &query)
{
    const std::string q = "collection:" + config_.librivox_collection + "%20AND%20(" +
                          url_encode(query) + ")";
    const std::string url = config_.archive_search_url + "?q=" + q +
                            "&fl[]=identifier,title,creator,runtime&rows=" +
                            std::to_string(config_.search_limit) + "&page=1&output=json";
    const std::string response = curl_fetch(url, true);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const auto docs = archive_docs_from_response(response, config_.search_limit);
    std::ostringstream out;
    out << "{\"ok\":true,\"source\":\"librivox\",\"results\":[";
    uint32_t count = 0;
    for (const std::string &doc : docs) {
        const std::string identifier = json_get_string(doc, "identifier");
        const std::string title = json_get_string(doc, "title");
        if (identifier.empty() || title.empty()) {
            continue;
        }
        const std::string runtime = json_get_string(doc, "runtime");
        if (count > 0) {
            out << ',';
        }
        out << "{\"id\":\"" << json_escape(identifier) << "\",\"title\":\"" << json_escape(title)
            << "\",\"author\":\"" << json_escape(creator_from_archive_doc(doc))
            << "\",\"source\":\"librivox\"";
        if (!runtime.empty()) {
            out << ",\"detail\":\"" << json_escape(runtime) << "\"";
        }
        out << '}';
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string LibraryBackend::download_gutendex(const std::string &result_id)
{
    const int gutenberg_id = std::atoi(result_id.c_str());
    if (gutenberg_id <= 0) {
        return "{\"ok\":false,\"error\":\"invalid id\"}";
    }

    const std::string url = config_.gutendex_url + "/" + std::to_string(gutenberg_id) + "/";
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"book lookup failed\"}";
    }

    const std::string title = json_get_string(response, "title");
    const std::string author = author_from_gutendex(response);
    const std::string download_url = pick_download_url(response, config_.preferred_format);
    if (download_url.empty()) {
        return "{\"ok\":false,\"error\":\"no downloadable format\"}";
    }

    const std::string format = format_from_url(download_url);
    const std::string ext = format == "epub" ? ".epub" : ".txt";
    ensure_directory(config_.download_dir);
    const std::string dest = config_.download_dir + "/" + sanitize_filename(title) + "-" +
                             std::to_string(gutenberg_id) + ext;

    const std::string download_result = download_file(download_url, dest);
    if (!json_get_bool(download_result, "ok", false)) {
        return download_result;
    }

    register_downloaded_book("gutendex", result_id, title, author, dest, format, gutenberg_id);
    return "{\"ok\":true,\"title\":\"" + json_escape(title) + "\",\"author\":\"" +
           json_escape(author) + "\",\"format\":\"" + json_escape(format) +
           "\",\"path\":\"" + json_escape(dest) + "\",\"source\":\"gutendex\",\"gutenberg_id\":" +
           std::to_string(gutenberg_id) + "}";
}

std::string LibraryBackend::download_openlibrary(const std::string &result_id)
{
    const std::string work_key = work_key_from_id(result_id);
    const std::string work_url = config_.openlibrary_url + "/works/" + work_key + ".json";
    const std::string work_json = curl_fetch(work_url);
    std::string title = json_get_string(work_json, "title");
    if (title.empty()) {
        title = work_key;
    }

    std::string ia_id = extract_string_array_first(work_json, "ia");
    if (ia_id.empty()) {
        const std::string editions_url =
            config_.openlibrary_url + "/works/" + work_key + "/editions.json?limit=5";
        const std::string editions_json = curl_fetch(editions_url);
        const std::string entries_body = extract_array_body(editions_json, "entries");
        for (const std::string &edition : json_split_objects("[" + entries_body + "]")) {
            ia_id = extract_string_array_first(edition, "ia");
            if (!ia_id.empty()) {
                break;
            }
        }
    }
    if (ia_id.empty()) {
        return "{\"ok\":false,\"error\":\"no archive edition found\"}";
    }

    const std::string metadata_url = config_.archive_metadata_url + "/" + ia_id;
    const std::string metadata = curl_fetch(metadata_url, true);
    if (metadata.empty()) {
        return "{\"ok\":false,\"error\":\"metadata lookup failed\"}";
    }

    const std::string author = creator_from_archive_doc(metadata);
    const std::string filename = pick_archive_file(metadata, "text");
    if (filename.empty()) {
        return "{\"ok\":false,\"error\":\"no downloadable format\"}";
    }

    const std::string format = format_from_url(filename);
    const std::string ext = filename.find('.') != std::string::npos
                                ? filename.substr(filename.find_last_of('.'))
                                : ".epub";
    ensure_directory(config_.download_dir);
    const std::string dest =
        config_.download_dir + "/" + sanitize_filename(title) + "-" + ia_id + ext;
    const std::string download_url =
        config_.archive_download_url + "/" + ia_id + "/" + url_encode(filename);

    const std::string download_result = download_file(download_url, dest);
    if (!json_get_bool(download_result, "ok", false)) {
        return download_result;
    }

    register_downloaded_book("openlibrary", result_id, title, author, dest, format);
    return "{\"ok\":true,\"title\":\"" + json_escape(title) + "\",\"author\":\"" +
           json_escape(author) + "\",\"format\":\"" + json_escape(format) +
           "\",\"path\":\"" + json_escape(dest) + "\",\"source\":\"openlibrary\"}";
}

std::string LibraryBackend::download_archive(const std::string &result_id)
{
    const std::string metadata_url = config_.archive_metadata_url + "/" + result_id;
    const std::string metadata = curl_fetch(metadata_url, true);
    if (metadata.empty()) {
        return "{\"ok\":false,\"error\":\"metadata lookup failed\"}";
    }

    const std::string metadata_obj = find_json_object(metadata, "metadata");
    std::string title = json_get_string(metadata_obj.empty() ? metadata : metadata_obj, "title");
    if (title.empty()) {
        title = result_id;
    }
    const std::string author = creator_from_archive_doc(
        metadata_obj.empty() ? metadata : metadata_obj);
    const std::string filename = pick_archive_file(metadata, "text");
    if (filename.empty()) {
        return "{\"ok\":false,\"error\":\"no downloadable format\"}";
    }

    const std::string format = format_from_url(filename);
    const std::string ext = filename.find('.') != std::string::npos
                                ? filename.substr(filename.find_last_of('.'))
                                : ".epub";
    ensure_directory(config_.download_dir);
    const std::string dest =
        config_.download_dir + "/" + sanitize_filename(title) + "-" + result_id + ext;
    const std::string download_url = config_.archive_download_url + "/" + result_id + "/" +
                                     url_encode(filename);

    const std::string download_result = download_file(download_url, dest);
    if (!json_get_bool(download_result, "ok", false)) {
        return download_result;
    }

    register_downloaded_book("archive", result_id, title, author, dest, format);
    return "{\"ok\":true,\"title\":\"" + json_escape(title) + "\",\"author\":\"" +
           json_escape(author) + "\",\"format\":\"" + json_escape(format) +
           "\",\"path\":\"" + json_escape(dest) + "\",\"source\":\"archive\"}";
}

std::string LibraryBackend::download_librivox(const std::string &result_id)
{
    const std::string metadata_url = config_.archive_metadata_url + "/" + result_id;
    const std::string metadata = curl_fetch(metadata_url, true);
    if (metadata.empty()) {
        return "{\"ok\":false,\"error\":\"metadata lookup failed\"}";
    }

    const std::string metadata_obj = find_json_object(metadata, "metadata");
    std::string title = json_get_string(metadata_obj.empty() ? metadata : metadata_obj, "title");
    if (title.empty()) {
        title = result_id;
    }
    const std::string author = creator_from_archive_doc(
        metadata_obj.empty() ? metadata : metadata_obj);
    const std::string filename = pick_archive_file(metadata, "audio");
    if (filename.empty()) {
        return "{\"ok\":false,\"error\":\"no audio download found\"}";
    }

    const std::string format = format_from_url(filename);
    const std::string ext = filename.find('.') != std::string::npos
                                ? filename.substr(filename.find_last_of('.'))
                                : ".zip";
    ensure_directory(config_.download_dir);
    const std::string dest =
        config_.download_dir + "/" + sanitize_filename(title) + "-" + result_id + ext;
    const std::string download_url = config_.archive_download_url + "/" + result_id + "/" +
                                     url_encode(filename);

    const std::string download_result = download_file(download_url, dest);
    if (!json_get_bool(download_result, "ok", false)) {
        return download_result;
    }

    register_downloaded_book("librivox", result_id, title, author, dest, format);
    return "{\"ok\":true,\"title\":\"" + json_escape(title) + "\",\"author\":\"" +
           json_escape(author) + "\",\"format\":\"" + json_escape(format) +
           "\",\"path\":\"" + json_escape(dest) + "\",\"source\":\"librivox\"}";
}

std::string LibraryBackend::list_local() const
{
    if (!file_exists(config_.catalog_path)) {
        return "{\"ok\":true,\"books\":[]}";
    }
    std::ifstream in(config_.catalog_path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    const std::string books_body = extract_array_body(json, "books");
    if (books_body.empty()) {
        return "{\"ok\":true,\"books\":[]}";
    }
    return "{\"ok\":true,\"books\":[" + books_body + "]}";
}

std::string LibraryBackend::status() const
{
    const std::string catalog = list_local();
    const size_t count_pos = catalog.find("\"books\":[");
    int count = 0;
    if (count_pos != std::string::npos) {
        const std::string body = extract_array_body(catalog, "books");
        count = static_cast<int>(json_split_objects("[" + body + "]").size());
    }
    return "{\"ok\":true,\"enabled\":" + std::string(config_.enabled ? "true" : "false") +
           ",\"local_count\":" + std::to_string(count) +
           ",\"sources\":[\"gutendex\",\"openlibrary\",\"librivox\"]}";
}

} // namespace braillatron::connect
