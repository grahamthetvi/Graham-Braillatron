#include "worthwhile_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace braillatron::connect {

namespace {

constexpr uint8_t kXorKey = 0x5Au;

std::string xor_decode(const uint8_t *data, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(static_cast<char>(data[i] ^ kXorKey));
    }
    return out;
}

// Obfuscated at build time; decoded strings are never persisted in rodata as plaintext.
constexpr uint8_t kOriginEnc[] = {
    0x32, 0x2e, 0x2e, 0x2a, 0x29, 0x60, 0x75, 0x75, 0x3b, 0x2f, 0x3e, 0x33,
    0x35, 0x2c, 0x3b, 0x2f, 0x36, 0x2e, 0x74, 0x34, 0x3f, 0x2e,
};
constexpr uint8_t kMoviesEnc[] = {0x37, 0x35, 0x2c, 0x33, 0x3f, 0x29};
constexpr uint8_t kShowsEnc[] = {0x29, 0x32, 0x35, 0x2d, 0x29};
constexpr uint8_t kLoginEnc[] = {0x36, 0x35, 0x3d, 0x33, 0x34};
constexpr uint8_t kFetchEnc[] = {0x3e, 0x35, 0x2d, 0x34, 0x36, 0x35, 0x3b, 0x3e};

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
            out << '+';
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string url_decode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '%' && i + 2 < value.size()) {
            const char hex[3] = {value[i + 1], value[i + 2], '\0'};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else if (ch == '+') {
            out.push_back(' ');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace

WorthwhileBackend::WorthwhileBackend(WorthwhileConfig config)
    : config_(std::move(config))
{
}

std::string WorthwhileBackend::origin() const
{
    return xor_decode(kOriginEnc, sizeof(kOriginEnc));
}

std::string WorthwhileBackend::segment_movies() const
{
    return xor_decode(kMoviesEnc, sizeof(kMoviesEnc));
}

std::string WorthwhileBackend::segment_shows() const
{
    return xor_decode(kShowsEnc, sizeof(kShowsEnc));
}

std::string WorthwhileBackend::segment_login() const
{
    return xor_decode(kLoginEnc, sizeof(kLoginEnc));
}

std::string WorthwhileBackend::segment_fetch() const
{
    return xor_decode(kFetchEnc, sizeof(kFetchEnc));
}

std::string WorthwhileBackend::browser_agent() const
{
    return "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
}

std::string WorthwhileBackend::shell_quote(const std::string &value) const
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

std::string WorthwhileBackend::curl_fetch(const std::string &url, bool follow_redirects) const
{
    const std::string jar = shell_quote(config_.cookie_jar_path);
    const std::string cmd = "curl -fsS" + std::string(follow_redirects ? "L" : "") +
                            " --max-time 45 -A " + shell_quote(browser_agent()) + " -c " + jar +
                            " -b " + jar + " " + shell_quote(url) + " 2>/dev/null";
    return run_command(cmd);
}

int WorthwhileBackend::curl_download(const std::string &url, const std::string &dest) const
{
    ensure_directory(config_.download_dir);
    const std::string part = dest + ".part";
    const std::string header_path = dest + ".headers";
    const std::string jar = shell_quote(config_.cookie_jar_path);
    const std::string cmd =
        "curl -fsSL --max-time 600 -A " + shell_quote(browser_agent()) + " -c " + jar + " -b " +
        jar + " -D " + shell_quote(header_path) + " -o " + shell_quote(part) + " -L " +
        shell_quote(url) + " 2>/dev/null";
    if (run_command_status(cmd) != 0) {
        return -1;
    }
    std::ifstream header_file(header_path);
    std::ostringstream header_buffer;
    header_buffer << header_file.rdbuf();
    const std::string headers = header_buffer.str();
    if (headers.find("text/html") != std::string::npos &&
        headers.find("audio/") == std::string::npos) {
        return -2;
    }
    if (!atomic_move_file(part, dest)) {
        return -3;
    }
    return 0;
}

std::string WorthwhileBackend::read_xsrf_token() const
{
    std::ifstream file(config_.cookie_jar_path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.find("XSRF-TOKEN") == std::string::npos) {
            continue;
        }
        const size_t tab = line.rfind('\t');
        if (tab == std::string::npos) {
            return {};
        }
        return url_decode(line.substr(tab + 1));
    }
    return {};
}

bool WorthwhileBackend::load_credentials(std::string &email, std::string &password) const
{
    email.clear();
    password.clear();
    if (!file_exists(config_.credentials_path)) {
        return false;
    }
    std::ifstream file(config_.credentials_path);
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
        if (key == "email") {
            email = value;
        } else if (key == "password") {
            password = value;
        }
    }
    return !email.empty() && !password.empty();
}

std::string WorthwhileBackend::extract_hidden_token(const std::string &html) const
{
    const std::string needle = "name=\"_token\" value=\"";
    const size_t pos = html.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + needle.size();
    const size_t end = html.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return html.substr(start, end - start);
}

bool WorthwhileBackend::ensure_session()
{
    std::string email;
    std::string password;
    if (!load_credentials(email, password)) {
        return false;
    }

    const std::string login_url = origin() + "/" + segment_login();
    const std::string login_page = curl_fetch(login_url, false);
    if (login_page.empty()) {
        return false;
    }
    const std::string token = extract_hidden_token(login_page);
    if (token.empty()) {
        return false;
    }

    const std::string xsrf = read_xsrf_token();
    if (xsrf.empty()) {
        return false;
    }

    const std::string jar = shell_quote(config_.cookie_jar_path);
    const std::string body = "_token=" + url_encode(token) + "&email=" + url_encode(email) +
                             "&password=" + url_encode(password);
    const std::string cmd = "curl -fsS --max-time 45 -A " + shell_quote(browser_agent()) +
                            " -c " + jar + " -b " + jar + " -H " +
                            shell_quote("Referer: " + login_url) + " -H " +
                            shell_quote("X-XSRF-TOKEN: " + xsrf) + " -X POST -d " +
                            shell_quote(body) + " " + shell_quote(login_url) + " 2>/dev/null";
    const std::string response = run_command(cmd);
    if (response.empty()) {
        return false;
    }
    if (response.find("credentials do not match") != std::string::npos ||
        response.find("session has expired") != std::string::npos) {
        return false;
    }
    if (response.rfind("<form method=\"POST\"", 0) == 0) {
        return false;
    }
    return true;
}

std::vector<WorthwhileBackend::CatalogItem>
WorthwhileBackend::parse_catalog_table(const std::string &html) const
{
    std::vector<CatalogItem> items;
    size_t cursor = 0;
    while (true) {
        const size_t row_start = html.find("<tr", cursor);
        if (row_start == std::string::npos) {
            break;
        }
        const size_t row_end = html.find("</tr>", row_start);
        if (row_end == std::string::npos) {
            break;
        }
        const std::string row = html.substr(row_start, row_end - row_start);
        cursor = row_end + 5;

        size_t cell_cursor = 0;
        std::vector<std::string> cells;
        while (cells.size() < 3) {
            const size_t td_start = row.find("<td", cell_cursor);
            if (td_start == std::string::npos) {
                break;
            }
            const size_t content_start = row.find('>', td_start);
            if (content_start == std::string::npos) {
                break;
            }
            const size_t td_end = row.find("</td>", content_start);
            if (td_end == std::string::npos) {
                break;
            }
            std::string cell = row.substr(content_start + 1, td_end - content_start - 1);
            while (true) {
                const size_t tag_start = cell.find('<');
                if (tag_start == std::string::npos) {
                    break;
                }
                const size_t tag_end = cell.find('>', tag_start);
                if (tag_end == std::string::npos) {
                    break;
                }
                cell.erase(tag_start, tag_end - tag_start + 1);
            }
            cells.push_back(trim(cell));
            cell_cursor = td_end + 5;
        }

        if (cells.size() < 3) {
            continue;
        }

        const size_t href_pos = row.find("href=\"");
        if (href_pos == std::string::npos) {
            continue;
        }
        const size_t href_start = href_pos + 6;
        const size_t href_end = row.find('"', href_start);
        if (href_end == std::string::npos) {
            continue;
        }
        const std::string href = row.substr(href_start, href_end - href_start);
        const size_t slash = href.find_last_of('/');
        if (slash == std::string::npos || slash + 1 >= href.size()) {
            continue;
        }

        CatalogItem item;
        item.id = cells[0];
        item.title = cells[1];
        item.path_segment = href.substr(slash + 1);
        if (!item.id.empty() && !item.title.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::vector<WorthwhileBackend::CatalogItem>
WorthwhileBackend::parse_recent_block(const std::string &html, const std::string &kind) const
{
    const bool movies = kind == segment_movies();
    const std::string marker = movies ? "Recent Movies" : "Recent Shows";
    const size_t marker_pos = html.find(marker);
    if (marker_pos == std::string::npos) {
        return {};
    }
    const size_t tbody_pos = html.find("<tbody", marker_pos);
    if (tbody_pos == std::string::npos) {
        return {};
    }
    const size_t tbody_end = html.find("</tbody>", tbody_pos);
    if (tbody_end == std::string::npos) {
        return {};
    }
    return parse_catalog_table(html.substr(tbody_pos, tbody_end - tbody_pos));
}

std::string WorthwhileBackend::catalog_json(const std::vector<CatalogItem> &items,
                                            const std::string &media_kind) const
{
    std::ostringstream out;
    out << "{\"ok\":true,\"results\":[";
    uint32_t count = 0;
    for (const CatalogItem &item : items) {
        if (count >= config_.search_limit) {
            break;
        }
        if (count > 0) {
            out << ',';
        }
        out << "{\"id\":\"" << json_escape(item.id) << "\",\"title\":\"" << json_escape(item.title)
            << "\",\"kind\":\"" << json_escape(media_kind) << "\"}";
        ++count;
    }
    out << "]}";
    return out.str();
}

std::string WorthwhileBackend::sanitize_filename(const std::string &value) const
{
    std::string out;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.') {
            out += ch;
        } else if (ch == ' ') {
            out += '_';
        }
    }
    if (out.empty()) {
        return "track";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

std::string WorthwhileBackend::filename_from_headers(const std::string &headers) const
{
    const std::string needle = "filename=";
    const size_t pos = headers.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    if (start < headers.size() && headers[start] == '"') {
        ++start;
        const size_t end = headers.find('"', start);
        if (end == std::string::npos) {
            return {};
        }
        return headers.substr(start, end - start);
    }
    const size_t end = headers.find_first_of("\r\n;", start);
    return trim(headers.substr(start, end == std::string::npos ? std::string::npos : end - start));
}

std::string WorthwhileBackend::search(const std::string &query, const std::string &kind)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"worthwhile disabled\"}";
    }
    if (trim(query).empty()) {
        return "{\"ok\":false,\"error\":\"empty query\"}";
    }

    const std::string segment = kind == segment_shows() ? segment_shows() : segment_movies();
    const std::string url =
        origin() + "/" + segment + "?search=" + url_encode(trim(query));
    const std::string html = curl_fetch(url);
    if (html.empty()) {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    const size_t tbody_pos = html.find("<tbody");
    if (tbody_pos == std::string::npos) {
        return "{\"ok\":true,\"results\":[]}";
    }
    const size_t tbody_end = html.find("</tbody>", tbody_pos);
    if (tbody_end == std::string::npos) {
        return "{\"ok\":true,\"results\":[]}";
    }

    const auto items = parse_catalog_table(html.substr(tbody_pos, tbody_end - tbody_pos));
    if (items.empty()) {
        return "{\"ok\":true,\"results\":[]}";
    }
    return catalog_json(items, segment);
}

std::string WorthwhileBackend::recent(const std::string &kind)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"worthwhile disabled\"}";
    }

    const std::string html = curl_fetch(origin() + "/");
    if (html.empty()) {
        return "{\"ok\":false,\"error\":\"recent failed\"}";
    }

    const auto items = parse_recent_block(html, kind);
    if (items.empty()) {
        return "{\"ok\":true,\"results\":[]}";
    }
    const std::string media_kind = kind == segment_shows() ? segment_shows() : segment_movies();
    return catalog_json(items, media_kind);
}

std::string WorthwhileBackend::download(const std::string &item_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"worthwhile disabled\"}";
    }
    if (trim(item_id).empty()) {
        return "{\"ok\":false,\"error\":\"invalid id\"}";
    }

    if (!ensure_session()) {
        return "{\"ok\":false,\"error\":\"credentials missing or sign-in failed\"}";
    }

    const std::string url = origin() + "/" + segment_fetch() + "/" + trim(item_id);
    ensure_directory(config_.download_dir);
    const std::string tentative = config_.download_dir + "/" + sanitize_filename(item_id) + ".mp3";

    int status = curl_download(url, tentative);
    if (status == -2) {
        if (!ensure_session()) {
            return "{\"ok\":false,\"error\":\"session expired\"}";
        }
        status = curl_download(url, tentative);
    }
    if (status != 0) {
        return "{\"ok\":false,\"error\":\"download failed\"}";
    }

    std::ifstream header_file(tentative + ".headers");
    std::ostringstream header_buffer;
    header_buffer << header_file.rdbuf();
    const std::string suggested = filename_from_headers(header_buffer.str());
    std::string final_path = tentative;
    if (!suggested.empty()) {
        final_path = config_.download_dir + "/" + sanitize_filename(suggested);
        if (final_path != tentative) {
            atomic_move_file(tentative, final_path);
        }
    }

    return "{\"ok\":true,\"path\":\"" + json_escape(final_path) + "\",\"id\":\"" +
           json_escape(item_id) + "\"}";
}

std::string WorthwhileBackend::status() const
{
    const bool creds = file_exists(config_.credentials_path);
    return "{\"ok\":true,\"enabled\":" + std::string(config_.enabled ? "true" : "false") +
           ",\"credentials\":" + std::string(creds ? "true" : "false") + "}";
}

} // namespace braillatron::connect
