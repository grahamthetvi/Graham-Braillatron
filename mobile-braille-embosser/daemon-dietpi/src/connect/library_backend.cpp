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
    const size_t end = json.find(']', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

std::string author_from_result(const std::string &result)
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
        const std::string epub =
            json_get_string(formats, "application/epub+zip");
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

} // namespace

LibraryBackend::LibraryBackend(LibraryConfig config)
    : config_(std::move(config))
{
}

std::string LibraryBackend::curl_fetch(const std::string &url) const
{
    const std::string cmd = "curl -fsSL --max-time 30 -A " + std::string(kUserAgent) + " " +
                            "'" + url + "' 2>/dev/null";
    return run_command(cmd);
}

std::string LibraryBackend::download_file(const std::string &url, const std::string &dest) const
{
    ensure_directory(fs::path(dest).parent_path().string());
    const std::string part = dest + ".part";
    const std::string cmd = "curl -fsSL --max-time 120 -A " + std::string(kUserAgent) + " -o " +
                            "'" + part + "' '" + url + "' 2>/dev/null";
    if (run_command_status(cmd) != 0) {
        return "{\"ok\":false,\"error\":\"download failed\"}";
    }
    if (!atomic_move_file(part, dest)) {
        return "{\"ok\":false,\"error\":\"save failed\"}";
    }
    return "{\"ok\":true,\"path\":\"" + json_escape(dest) + "\"}";
}

bool LibraryBackend::register_downloaded_book(int gutenberg_id, const std::string &title,
                                              const std::string &author,
                                              const std::string &local_path,
                                              const std::string &format)
{
    ensure_directory(fs::path(config_.catalog_path).parent_path().string());

    std::vector<std::string> entries;
    if (file_exists(config_.catalog_path)) {
        std::ifstream in(config_.catalog_path);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string json = buffer.str();
        for (const auto &obj : json_split_objects("[" + extract_array_body(json, "books") + "]")) {
            entries.push_back(obj);
            if (json_get_int_value(obj, "gutenberg_id", 0) == gutenberg_id) {
                return true;
            }
        }
    }

    std::string next_id = "lib-" + std::to_string(gutenberg_id);
    std::ostringstream book;
    book << "{\n"
         << "      \"id\":\"" << json_escape(next_id) << "\",\n"
         << "      \"title\":\"" << json_escape(title) << "\",\n"
         << "      \"author\":\"" << json_escape(author) << "\",\n"
         << "      \"format\":\"" << json_escape(format) << "\",\n"
         << "      \"local_path\":\"" << json_escape(local_path) << "\",\n"
         << "      \"source\":\"gutendex\",\n"
         << "      \"gutenberg_id\":" << gutenberg_id << "\n"
         << "    }";
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

std::string LibraryBackend::search(const std::string &query)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"library disabled\"}";
    }
    if (trim(query).empty()) {
        return "{\"ok\":false,\"error\":\"empty query\"}";
    }

    const std::string url = config_.gutendex_url + "/?search=" + url_encode(query);
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const std::string results_body = extract_array_body(response, "results");
    if (results_body.empty()) {
        return "{\"ok\":true,\"results\":[]}";
    }

    const auto objects = json_split_objects("[" + results_body + "]");
    std::ostringstream out;
    out << "{\"ok\":true,\"results\":[";
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
        out << "{\"id\":" << id << ",\"title\":\"" << json_escape(title) << "\",\"author\":\""
            << json_escape(author_from_result(result)) << "\"}";
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string LibraryBackend::download(int gutenberg_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"library disabled\"}";
    }
    if (gutenberg_id <= 0) {
        return "{\"ok\":false,\"error\":\"invalid id\"}";
    }

    const std::string url = config_.gutendex_url + "/" + std::to_string(gutenberg_id) + "/";
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"book lookup failed\"}";
    }

    const std::string title = json_get_string(response, "title");
    const std::string author = author_from_result(response);
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

    register_downloaded_book(gutenberg_id, title, author, dest, format);
    return "{\"ok\":true,\"title\":\"" + json_escape(title) + "\",\"author\":\"" +
           json_escape(author) + "\",\"format\":\"" + json_escape(format) +
           "\",\"path\":\"" + json_escape(dest) + "\",\"gutenberg_id\":" +
           std::to_string(gutenberg_id) + "}";
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
           ",\"local_count\":" + std::to_string(count) + "}";
}

} // namespace braillatron::connect
