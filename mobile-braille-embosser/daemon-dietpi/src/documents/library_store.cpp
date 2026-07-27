#include "library_store.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace braillatron::documents {

namespace fs = std::filesystem;

namespace {

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

std::string lower_copy(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool parse_bool(const std::string &value)
{
    const std::string lower = trim(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::string sanitize_filename(std::string value)
{
    value = trim(value);
    for (char &ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_' && ch != '.') {
            ch = '_';
        }
    }
    return value;
}

bool has_extension(const std::string &path, const std::string &ext)
{
    const std::string lower = lower_copy(path);
    return lower.size() >= ext.size() && lower.substr(lower.size() - ext.size()) == ext;
}

void collect_mount_points(const std::string &root, std::vector<std::string> &out)
{
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return;
    }
    for (const auto &entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string path = entry.path().string();
        if (path.rfind("/data", 0) == 0) {
            continue;
        }
        out.push_back(path);
    }
}

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 4);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

std::string parse_json_string(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < json.size() &&
           std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    if (start >= json.size() || json[start] != '"') {
        return {};
    }
    ++start;
    const size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

int parse_json_int(const std::string &json, const std::string &key, int default_value)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    size_t start = pos + needle.size();
    while (start < json.size() &&
           std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    return std::atoi(json.c_str() + start);
}

uint64_t parse_json_uint64(const std::string &json, const std::string &key, uint64_t default_value)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    size_t start = pos + needle.size();
    while (start < json.size() &&
           std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    return std::strtoull(json.c_str() + start, nullptr, 10);
}

std::string shell_quote(const std::string &value)
{
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

std::string xml_attr_value(const std::string &xml, const std::string &tag, const std::string &attr)
{
    const std::string open = "<" + tag;
    size_t pos = 0;
    while ((pos = xml.find(open, pos)) != std::string::npos) {
        const size_t tag_end = xml.find('>', pos);
        if (tag_end == std::string::npos) {
            break;
        }
        const std::string tag_block = xml.substr(pos, tag_end - pos);
        const std::string needle = attr + "=\"";
        const size_t attr_pos = tag_block.find(needle);
        if (attr_pos != std::string::npos) {
            const size_t start = attr_pos + needle.size();
            const size_t end = tag_block.find('"', start);
            if (end != std::string::npos) {
                return tag_block.substr(start, end - start);
            }
        }
        pos = tag_end + 1;
    }
    return {};
}

std::string xml_tag_text(const std::string &xml, const std::string &tag)
{
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    size_t pos = xml.find(open);
    if (pos == std::string::npos) {
        return {};
    }
    pos = xml.find('>', pos);
    if (pos == std::string::npos) {
        return {};
    }
    ++pos;
    const size_t end = xml.find(close, pos);
    if (end == std::string::npos) {
        return {};
    }
    return trim(xml.substr(pos, end - pos));
}

std::string decode_xml_entities(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '&') {
            out += value[i];
            continue;
        }
        const size_t semi = value.find(';', i);
        if (semi == std::string::npos) {
            out += value[i];
            continue;
        }
        const std::string entity = value.substr(i, semi - i + 1);
        if (entity == "&amp;") {
            out += '&';
        } else if (entity == "&lt;") {
            out += '<';
        } else if (entity == "&gt;") {
            out += '>';
        } else if (entity == "&quot;") {
            out += '"';
        } else if (entity == "&apos;") {
            out += '\'';
        } else {
            out += entity;
        }
        i = semi;
    }
    return out;
}

std::string attr_value(const std::string &fragment, const std::string &attr)
{
    const std::string needle = attr + "=\"";
    const size_t attr_pos = fragment.find(needle);
    if (attr_pos == std::string::npos) {
        return {};
    }
    const size_t start = attr_pos + needle.size();
    const size_t end = fragment.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return fragment.substr(start, end - start);
}

} // namespace

LibraryStoreConfig load_library_store_config(const std::string &path)
{
    LibraryStoreConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "catalog_path") {
            config.catalog_path = value;
        } else if (key == "books_dir") {
            config.books_dir = value;
        } else if (key == "import_dir") {
            config.import_dir = value;
        } else if (key == "state_dir") {
            config.state_dir = value;
        } else if (key == "epub_enabled") {
            config.epub_enabled = parse_bool(value);
        } else if (key == "daisy_enabled") {
            config.daisy_enabled = parse_bool(value);
        } else if (key == "max_local_results") {
            config.max_local_results = std::max(1, std::atoi(value.c_str()));
        }
    }
    return config;
}

std::string EbookDocument::read_file(const std::string &path) const
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string EbookDocument::html_to_text(const std::string &html) const
{
    std::string text = html;
    size_t pos = 0;
    while ((pos = text.find('<', pos)) != std::string::npos) {
        const size_t end = text.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        text.erase(pos, end - pos + 1);
    }
    text = decode_xml_entities(text);
    std::string collapsed;
    collapsed.reserve(text.size());
    bool last_space = false;
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!last_space) {
                collapsed.push_back(' ');
                last_space = true;
            }
        } else {
            collapsed.push_back(ch);
            last_space = false;
        }
    }
    return trim(collapsed);
}

std::string EbookDocument::find_opf_path(const std::string &container_xml) const
{
    const std::string path = xml_attr_value(container_xml, "rootfile ", "full-path");
    if (!path.empty()) {
        return path;
    }
    const size_t attr = container_xml.find("full-path=\"");
    if (attr == std::string::npos) {
        return {};
    }
    const size_t start = attr + 11;
    const size_t end = container_xml.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return container_xml.substr(start, end - start);
}

bool EbookDocument::parse_opf(const std::string &opf_path, const std::string &base_dir)
{
    const std::string opf_content = read_file(opf_path);
    if (opf_content.empty()) {
        return false;
    }

    title_ = xml_tag_text(opf_content, "dc:title");
    if (title_.empty()) {
        title_ = xml_tag_text(opf_content, "title");
    }
    author_ = xml_tag_text(opf_content, "dc:creator");
    if (author_.empty()) {
        author_ = xml_tag_text(opf_content, "creator");
    }

    load_spine_sections(opf_content, base_dir);

    const std::string ncx_id = [&]() {
        size_t pos = 0;
        while ((pos = opf_content.find("<item ", pos)) != std::string::npos) {
            const size_t end = opf_content.find("/>", pos);
            if (end == std::string::npos) {
                break;
            }
            const std::string item = opf_content.substr(pos, end - pos);
            const std::string media = attr_value(item, "media-type");
            if (media.find("ncx") != std::string::npos) {
                return attr_value(item, "href");
            }
            pos = end + 2;
        }
        return std::string {};
    }();

    if (!ncx_id.empty()) {
        const fs::path opf_dir = fs::path(opf_path).parent_path();
        apply_ncx_titles((opf_dir / ncx_id).string(), base_dir);
    }

    return !sections_.empty();
}

void EbookDocument::load_spine_sections(const std::string &opf_content, const std::string &base_dir)
{
    sections_.clear();

    std::unordered_map<std::string, std::string> manifest;
    size_t pos = 0;
    while ((pos = opf_content.find("<item ", pos)) != std::string::npos) {
        const size_t end = opf_content.find("/>", pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string item = opf_content.substr(pos, end - pos);
        const std::string id = attr_value(item, "id");
        const std::string href = attr_value(item, "href");
        if (!id.empty() && !href.empty()) {
            manifest[id] = href;
        }
        pos = end + 2;
    }

    int spine_index = 0;
    pos = 0;
    while ((pos = opf_content.find("<itemref ", pos)) != std::string::npos) {
        const size_t end = opf_content.find("/>", pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string itemref = opf_content.substr(pos, end - pos);
        const std::string idref = attr_value(itemref, "idref");
        const auto it = manifest.find(idref);
        if (it != manifest.end()) {
            const fs::path content_path = fs::path(base_dir) / it->second;
            const std::string html = read_file(content_path.string());
            if (!html.empty()) {
                BookSection section;
                section.id = idref;
                section.href = it->second;
                section.title = "Section " + std::to_string(spine_index + 1);
                section.text = html_to_text(html);
                section.spine_index = spine_index;
                if (!section.text.empty()) {
                    sections_.push_back(std::move(section));
                }
            }
        }
        ++spine_index;
        pos = end + 2;
    }
}

void EbookDocument::apply_ncx_titles(const std::string &ncx_path, const std::string &base_dir)
{
    (void)base_dir;
    const std::string ncx = read_file(ncx_path);
    if (ncx.empty()) {
        return;
    }

    struct NavEntry {
        std::string src;
        std::string title;
    };
    std::vector<NavEntry> entries;

    size_t pos = 0;
    while ((pos = ncx.find("<navPoint", pos)) != std::string::npos) {
        const size_t end = ncx.find("</navPoint>", pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string block = ncx.substr(pos, end - pos);
        NavEntry entry;
        entry.title = xml_tag_text(block, "text");
        const std::string src = attr_value(block, "src");
        const size_t hash = src.find('#');
        entry.src = hash == std::string::npos ? src : src.substr(0, hash);
        if (!entry.title.empty() && !entry.src.empty()) {
            entries.push_back(std::move(entry));
        }
        pos = end + 11;
    }

    for (BookSection &section : sections_) {
        for (const NavEntry &entry : entries) {
            if (section.href == entry.src || section.href.find(entry.src) != std::string::npos ||
                entry.src.find(section.href) != std::string::npos) {
                section.title = entry.title;
                break;
            }
        }
    }
}

bool EbookDocument::open_extracted_dir(const std::string &dir, const std::string &format)
{
    format_ = format;
    sections_.clear();
    title_.clear();
    author_.clear();

    const std::string container_path = dir + "/META-INF/container.xml";
    std::string opf_relative;
    if (fs::exists(container_path)) {
        opf_relative = find_opf_path(read_file(container_path));
    }

    if (opf_relative.empty()) {
        for (const auto &entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name == "content.opf" || (name.size() > 4 && name.substr(name.size() - 4) == ".opf")) {
                opf_relative = fs::relative(entry.path(), dir).string();
                break;
            }
        }
    }

    if (opf_relative.empty()) {
        return false;
    }

    const fs::path opf_path = fs::path(dir) / opf_relative;
    const std::string base_dir = opf_path.parent_path().string();
    return parse_opf(opf_path.string(), base_dir);
}

bool EbookDocument::open_epub_archive(const std::string &path)
{
    const std::string extract_dir = path + ".extracted";
    std::error_code ec;
    if (fs::exists(extract_dir, ec)) {
        fs::remove_all(extract_dir, ec);
    }
    fs::create_directories(extract_dir, ec);

    const std::string cmd =
        "unzip -o -q " + shell_quote(path) + " -d " + shell_quote(extract_dir) + " 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        return false;
    }
    return open_extracted_dir(extract_dir, "epub");
}

bool EbookDocument::open(const std::string &path)
{
    sections_.clear();
    title_.clear();
    author_.clear();
    format_.clear();

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return false;
    }

    if (fs::is_directory(path, ec)) {
        return open_extracted_dir(path, "daisy");
    }

    const std::string lower = lower_copy(path);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".epub") {
        return open_epub_archive(path);
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt") {
        format_ = "txt";
        title_ = fs::path(path).stem().string();
        BookSection section;
        section.id = "text";
        section.title = title_;
        section.text = read_file(path);
        section.spine_index = 0;
        if (section.text.empty()) {
            return false;
        }
        sections_.push_back(std::move(section));
        return true;
    }

    return false;
}

LibraryStore::LibraryStore(LibraryStoreConfig config)
    : config_(std::move(config))
{
}

bool LibraryStore::load()
{
    books_.clear();
    load_catalog();
    process_import_dir();
    return true;
}

void LibraryStore::refresh()
{
    load();
}

bool LibraryStore::load_catalog()
{
    std::ifstream in(config_.catalog_path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    if (json.find("\"books\"") == std::string::npos) {
        return false;
    }

    size_t pos = 0;
    while (true) {
        const size_t start = json.find('{', pos);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = json.find('}', start);
        if (end == std::string::npos) {
            break;
        }
        const std::string object = json.substr(start, end - start + 1);
        if (object.find("\"id\"") == std::string::npos) {
            pos = end + 1;
            continue;
        }

        LibraryBook book;
        book.id = parse_json_string(object, "id");
        book.title = parse_json_string(object, "title");
        book.author = parse_json_string(object, "author");
        book.format = parse_json_string(object, "format");
        book.local_path = parse_json_string(object, "local_path");
        book.source = parse_json_string(object, "source");
        book.gutenberg_id = parse_json_int(object, "gutenberg_id", 0);
        if (!book.id.empty() && !book.title.empty()) {
            books_.push_back(std::move(book));
        }
        pos = end + 1;
    }
    return true;
}

bool LibraryStore::save() const
{
    std::error_code ec;
    const fs::path path(config_.catalog_path);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }

    const std::string temp_path = config_.catalog_path + ".tmp";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n  \"books\":[\n";
    for (size_t i = 0; i < books_.size(); ++i) {
        const LibraryBook &book = books_[i];
        if (i > 0) {
            out << ",\n";
        }
        out << "    {\n"
            << "      \"id\":\"" << json_escape(book.id) << "\",\n"
            << "      \"title\":\"" << json_escape(book.title) << "\",\n"
            << "      \"author\":\"" << json_escape(book.author) << "\",\n"
            << "      \"format\":\"" << json_escape(book.format) << "\",\n"
            << "      \"local_path\":\"" << json_escape(book.local_path) << "\",\n"
            << "      \"source\":\"" << json_escape(book.source) << "\",\n"
            << "      \"gutenberg_id\":" << book.gutenberg_id << "\n"
            << "    }";
    }
    out << "\n  ]\n}\n";
    out.flush();
    if (!out.good()) {
        return false;
    }

    fs::rename(temp_path, config_.catalog_path, ec);
    return !ec;
}

std::string LibraryStore::next_id() const
{
    int max_id = 0;
    for (const LibraryBook &book : books_) {
        if (book.id.size() > 4 && book.id.substr(0, 4) == "lib-") {
            max_id = std::max(max_id, std::atoi(book.id.c_str() + 4));
        }
    }
    return "lib-" + std::to_string(max_id + 1 + id_counter_++);
}

std::vector<LibraryBook> LibraryStore::search_local(const std::string &query) const
{
    if (query.empty()) {
        std::vector<LibraryBook> all = books_;
        if (static_cast<int>(all.size()) > config_.max_local_results) {
            all.resize(static_cast<size_t>(config_.max_local_results));
        }
        return all;
    }

    const std::string needle = lower_copy(query);
    std::vector<LibraryBook> matches;
    for (const LibraryBook &book : books_) {
        if (lower_copy(book.title).find(needle) != std::string::npos ||
            lower_copy(book.author).find(needle) != std::string::npos) {
            matches.push_back(book);
            if (static_cast<int>(matches.size()) >= config_.max_local_results) {
                break;
            }
        }
    }
    return matches;
}

const LibraryBook *LibraryStore::find_by_id(const std::string &id) const
{
    for (const LibraryBook &book : books_) {
        if (book.id == id) {
            return &book;
        }
    }
    return nullptr;
}

bool LibraryStore::register_book(LibraryBook book)
{
    if (book.id.empty()) {
        book.id = next_id();
    }
    for (const LibraryBook &existing : books_) {
        if (existing.local_path == book.local_path && !book.local_path.empty()) {
            return false;
        }
        if (book.gutenberg_id > 0 && existing.gutenberg_id == book.gutenberg_id) {
            return false;
        }
    }
    books_.push_back(std::move(book));
    return save();
}

bool LibraryStore::remove_book(const std::string &id)
{
    const auto it = std::find_if(books_.begin(), books_.end(),
                                 [&](const LibraryBook &book) { return book.id == id; });
    if (it == books_.end()) {
        return false;
    }

    const std::string local_path = it->local_path;
    const std::string book_id = it->id;
    books_.erase(it);
    if (!save()) {
        return false;
    }

    std::error_code ec;
    const std::string state_path = config_.state_dir + "/" + book_id + ".json";
    fs::remove(state_path, ec);

    if (!local_path.empty() && fs::exists(local_path, ec)) {
        if (fs::is_directory(local_path, ec)) {
            fs::remove_all(local_path, ec);
        } else {
            fs::remove(local_path, ec);
            const std::string lower = lower_copy(local_path);
            if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".epub") {
                fs::remove_all(local_path + ".extracted", ec);
            }
        }
    }
    return true;
}

bool LibraryStore::rename_book(const std::string &id, const std::string &new_title)
{
    const std::string title = trim(new_title);
    if (title.empty()) {
        return false;
    }

    const auto it = std::find_if(books_.begin(), books_.end(),
                                 [&](const LibraryBook &book) { return book.id == id; });
    if (it == books_.end()) {
        return false;
    }

    it->title = title;

    std::error_code ec;
    if (!it->local_path.empty() && fs::exists(it->local_path, ec) && fs::is_regular_file(it->local_path, ec)) {
        const fs::path old_path(it->local_path);
        const std::string ext = old_path.extension().string();
        std::string stem = sanitize_filename(title);
        if (stem.empty()) {
            stem = "item";
        }
        fs::path new_path = old_path.parent_path() / (stem + ext);
        int suffix = 1;
        while (fs::exists(new_path, ec) && new_path != old_path) {
            new_path = old_path.parent_path() / (stem + "-" + std::to_string(suffix++) + ext);
        }
        if (new_path != old_path) {
            fs::rename(old_path, new_path, ec);
            if (!ec) {
                it->local_path = new_path.string();
            }
        }
    }

    return save();
}

bool LibraryStore::import_file(const std::string &src_path)
{
    std::error_code ec;
    if (!fs::exists(src_path, ec) || !fs::is_regular_file(src_path, ec)) {
        return false;
    }

    const std::string format = detect_format(src_path);
    if (format.empty()) {
        return false;
    }

    fs::create_directories(config_.books_dir, ec);
    const fs::path src(src_path);
    std::string filename = src.filename().string();
    fs::path dest = fs::path(config_.books_dir) / filename;
    int suffix = 1;
    while (fs::exists(dest, ec)) {
        dest = fs::path(config_.books_dir) /
               (src.stem().string() + "-" + std::to_string(suffix++) + src.extension().string());
    }

    const std::string temp_path = dest.string() + ".tmp";
    fs::copy_file(src, temp_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    fs::rename(temp_path, dest, ec);
    if (ec) {
        fs::remove(temp_path, ec);
        return false;
    }

    if (format == "mp3" || format == "m4a" || format == "ogg" || format == "flac" ||
        format == "wav") {
        return register_media_file(dest.string(), src.stem().string(), "usb");
    }

    if (format == "brf" || format == "txt") {
        LibraryBook book;
        book.title = src.stem().string();
        book.format = format;
        book.local_path = dest.string();
        book.source = "usb";
        return register_book(std::move(book));
    }

    EbookDocument doc;
    if (!doc.open(dest.string())) {
        fs::remove(dest, ec);
        return false;
    }

    LibraryBook book;
    book.title = doc.title().empty() ? src.stem().string() : doc.title();
    book.author = doc.author();
    book.format = format;
    book.local_path = dest.string();
    book.source = "usb";
    return register_book(std::move(book));
}

std::vector<std::string> LibraryStore::list_removable_mounts() const
{
    std::vector<std::string> mounts;
    collect_mount_points("/run/media", mounts);
    collect_mount_points("/media", mounts);
    collect_mount_points("/mnt", mounts);

    std::sort(mounts.begin(), mounts.end());
    mounts.erase(std::unique(mounts.begin(), mounts.end()), mounts.end());
    return mounts;
}

std::string LibraryStore::detect_format(const std::string &path) const
{
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return config_.daisy_enabled ? "daisy" : "";
    }
    const std::string lower = lower_copy(path);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".epub") {
        return config_.epub_enabled ? "epub" : "";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt") {
        return "txt";
    }
    if (has_extension(lower, ".brf")) {
        return "brf";
    }
    if (has_extension(lower, ".mp3")) {
        return "mp3";
    }
    if (has_extension(lower, ".m4a")) {
        return "m4a";
    }
    if (has_extension(lower, ".m4b")) {
        return "m4b";
    }
    if (has_extension(lower, ".aac")) {
        return "aac";
    }
    if (has_extension(lower, ".ogg")) {
        return "ogg";
    }
    if (has_extension(lower, ".flac")) {
        return "flac";
    }
    if (has_extension(lower, ".wav")) {
        return "wav";
    }
    return {};
}

bool LibraryStore::save_document_text(const std::string &text, const std::string &title_hint)
{
    if (trim(text).empty()) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(config_.books_dir, ec);

    std::string title = trim(title_hint);
    if (title.empty()) {
        const size_t newline = text.find('\n');
        title = trim(newline == std::string::npos ? text : text.substr(0, newline));
    }
    if (title.size() > 64) {
        title = title.substr(0, 61) + "...";
    }
    if (title.empty()) {
        title = "Document";
    }

    const std::time_t now = std::time(nullptr);
    const std::string filename = "doc-" + std::to_string(static_cast<long long>(now)) + ".txt";
    const std::string dest = (fs::path(config_.books_dir) / filename).string();
    const std::string temp_path = dest + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << text;
        out.flush();
        if (!out.good()) {
            return false;
        }
    }
    fs::rename(temp_path, dest, ec);
    if (ec) {
        return false;
    }

    LibraryBook book;
    book.title = title;
    book.format = "txt";
    book.local_path = dest;
    book.source = "document";
    return register_book(std::move(book));
}

bool LibraryStore::register_media_file(const std::string &path, const std::string &title,
                                       const std::string &source)
{
    const std::string format = detect_format(path);
    if (format.empty()) {
        return false;
    }

    LibraryBook book;
    book.title = title.empty() ? fs::path(path).stem().string() : title;
    book.format = format;
    book.local_path = path;
    book.source = source;
    return register_book(std::move(book));
}

bool LibraryStore::process_import_dir()
{
    std::error_code ec;
    if (!fs::exists(config_.import_dir, ec)) {
        return false;
    }
    fs::create_directories(config_.books_dir, ec);
    const fs::path processed = fs::path(config_.import_dir) / "processed";
    fs::create_directories(processed, ec);

    bool imported = false;
    for (const auto &entry : fs::directory_iterator(config_.import_dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string src = entry.path().string();
        const std::string format = detect_format(src);
        if (format.empty()) {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        const std::string dest = (fs::path(config_.books_dir) / filename).string();
        fs::rename(entry.path(), dest, ec);
        if (ec) {
            continue;
        }

        EbookDocument doc;
        if (!doc.open(dest)) {
            fs::rename(dest, processed / filename, ec);
            continue;
        }

        LibraryBook book;
        const fs::path stem_path(filename);
        book.title = doc.title().empty() ? stem_path.stem().string() : doc.title();
        book.author = doc.author();
        book.format = format;
        book.local_path = dest;
        book.source = "import";
        register_book(std::move(book));
        imported = true;
    }
    return imported;
}

ReadingState LibraryStore::load_reading_state(const std::string &book_id) const
{
    ReadingState state;
    state.book_id = book_id;
    const std::string path = config_.state_dir + "/" + book_id + ".json";
    std::ifstream in(path);
    if (!in.is_open()) {
        return state;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    state.section_index = parse_json_int(json, "section_index", 0);
    state.char_offset = parse_json_int(json, "char_offset", 0);
    state.updated_at = parse_json_uint64(json, "updated_at", 0);
    return state;
}

bool LibraryStore::save_reading_state(const ReadingState &state) const
{
    std::error_code ec;
    fs::create_directories(config_.state_dir, ec);
    const std::string path = config_.state_dir + "/" + state.book_id + ".json";
    const std::string temp_path = path + ".tmp";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }
    out << "{\n"
        << "  \"book_id\":\"" << json_escape(state.book_id) << "\",\n"
        << "  \"section_index\":" << state.section_index << ",\n"
        << "  \"char_offset\":" << state.char_offset << ",\n"
        << "  \"updated_at\":" << state.updated_at << "\n"
        << "}\n";
    out.flush();
    if (!out.good()) {
        return false;
    }
    fs::rename(temp_path, path, ec);
    return !ec;
}

} // namespace braillatron::documents
