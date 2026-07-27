#include "gmail_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace braillatron::connect {

namespace {

uint64_t unix_now_sec()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

std::string shell_escape(const std::string &value)
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

std::string url_encode_form(const std::string &value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else if (ch == ' ') {
            out << '+';
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

std::string decode_base64url(const std::string &input)
{
    std::string normalized = input;
    for (char &ch : normalized) {
        if (ch == '-') {
            ch = '+';
        } else if (ch == '_') {
            ch = '/';
        }
    }
    while (normalized.size() % 4 != 0) {
        normalized.push_back('=');
    }

    static const std::string kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(normalized.size() * 3 / 4);
    int val = 0;
    int valb = -8;
    for (unsigned char ch : normalized) {
        if (ch == '=') {
            break;
        }
        const size_t pos = kAlphabet.find(static_cast<char>(ch));
        if (pos == std::string::npos) {
            continue;
        }
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            output.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return output;
}

std::string trim_copy(const std::string &value)
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

std::string to_lower_copy(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string header_value_from_rfc822(const std::string &raw, const std::string &name)
{
    const std::string needle = name + ":";
    std::istringstream stream(raw);
    std::string line;
    std::string value;
    bool collecting = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (collecting) {
            if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
                value += ' ';
                value += trim_copy(line);
                continue;
            }
            break;
        }
        if (line.size() >= needle.size() &&
            to_lower_copy(line.substr(0, needle.size())) == to_lower_copy(needle)) {
            value = trim_copy(line.substr(needle.size()));
            collecting = true;
        }
        if (line.empty()) {
            break;
        }
    }
    return value;
}

std::string plain_body_from_rfc822(const std::string &raw)
{
    const size_t sep = raw.find("\r\n\r\n");
    const size_t sep2 = raw.find("\n\n");
    size_t body_start = std::string::npos;
    if (sep != std::string::npos && (sep2 == std::string::npos || sep <= sep2)) {
        body_start = sep + 4;
    } else if (sep2 != std::string::npos) {
        body_start = sep2 + 2;
    }
    if (body_start == std::string::npos || body_start >= raw.size()) {
        return {};
    }
    std::string body = raw.substr(body_start);
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) {
        body.pop_back();
    }
    return body;
}

std::vector<std::string> parse_imap_search_uids(const std::string &response)
{
    std::vector<std::string> uids;
    const size_t pos = response.find("* SEARCH");
    if (pos == std::string::npos) {
        return uids;
    }
    size_t i = pos + 8;
    while (i < response.size() && !std::isdigit(static_cast<unsigned char>(response[i]))) {
        ++i;
    }
    std::string current;
    for (; i < response.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(response[i]);
        if (std::isdigit(ch)) {
            current.push_back(static_cast<char>(ch));
        } else if (!current.empty()) {
            uids.push_back(current);
            current.clear();
            if (ch == '\n' || ch == '\r') {
                break;
            }
        } else if (ch == '\n' || ch == '\r') {
            break;
        }
    }
    if (!current.empty()) {
        uids.push_back(current);
    }
    return uids;
}

std::string encode_base64url(const std::string &input)
{
    static const char *kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    int val = 0;
    int valb = -6;
    for (unsigned char ch : input) {
        val = (val << 8) + ch;
        valb += 8;
        while (valb >= 0) {
            output.push_back(kAlphabet[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        output.push_back(kAlphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (output.size() % 4 != 0) {
        output.push_back('=');
    }
    for (char &ch : output) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    while (!output.empty() && output.back() == '=') {
        output.pop_back();
    }
    return output;
}

} // namespace

GmailBackend::GmailBackend(GmailConfig config, EventWriter *events)
    : config_(std::move(config))
    , events_(events)
{
    ensure_directory(config_.credentials_dir);
}

bool GmailBackend::is_oauth_linked() const
{
    std::string access;
    std::string refresh;
    uint64_t expires = 0;
    std::string email;
    return load_token(access, refresh, expires, email) && !refresh.empty();
}

bool GmailBackend::is_imap_linked() const
{
    std::string email;
    std::string password;
    std::string host_override;
    return load_imap_credentials(email, password, host_override);
}

bool GmailBackend::is_linked() const
{
    const std::string mode = to_lower_copy(config_.auth_mode);
    if (mode == "oauth") {
        return is_oauth_linked();
    }
    if (mode == "imap") {
        return is_imap_linked();
    }
    return is_oauth_linked() || is_imap_linked();
}

GmailBackend::Transport GmailBackend::active_transport() const
{
    const std::string mode = to_lower_copy(config_.auth_mode);
    if (mode == "oauth") {
        return Transport::Oauth;
    }
    if (mode == "imap") {
        return Transport::Imap;
    }
    // auto: prefer IMAP when linked so school accounts win over stale OAuth.
    if (is_imap_linked()) {
        return Transport::Imap;
    }
    return Transport::Oauth;
}

std::string GmailBackend::load_client_id() const
{
    std::ifstream file(config_.client_id_path);
    if (!file.is_open()) {
        return {};
    }
    std::string line;
    std::getline(file, line);
    size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    size_t end = line.size();
    while (end > start && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
        --end;
    }
    return line.substr(start, end - start);
}

bool GmailBackend::load_token(std::string &access_token, std::string &refresh_token,
                              uint64_t &expires_at, std::string &email) const
{
    const std::string json = run_command("cat " + shell_escape(config_.token_path) + " 2>/dev/null");
    if (json.empty() || json.find('{') == std::string::npos) {
        return false;
    }
    access_token = json_get_string(json, "access_token");
    refresh_token = json_get_string(json, "refresh_token");
    email = json_get_string(json, "email");
    const std::string expires = json_get_string(json, "expires_at");
    expires_at = expires.empty() ? 0 : std::stoull(expires);
    return !refresh_token.empty();
}

bool GmailBackend::save_token(const std::string &access_token, const std::string &refresh_token,
                              uint64_t expires_at, const std::string &email) const
{
    const std::string tmp = config_.token_path + ".tmp";
    std::ostringstream out;
    out << "{\"access_token\":\"" << json_escape(access_token) << "\",\"refresh_token\":\""
        << json_escape(refresh_token) << "\",\"expires_at\":\"" << expires_at << "\",\"email\":\""
        << json_escape(email) << "\"}";
    std::ofstream file(tmp);
    if (!file.is_open()) {
        return false;
    }
    file << out.str();
    file.close();
    run_command("chmod 600 " + shell_escape(tmp));
    return atomic_move_file(tmp, config_.token_path);
}

std::string GmailBackend::link_status() const
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }
    const bool oauth_linked = is_oauth_linked();
    const bool imap_linked = is_imap_linked();
    const bool linked = is_linked();
    const bool pending = link_watch_active_.load();
    const std::string transport =
        active_transport() == Transport::Imap ? "imap" : "oauth";
    std::ostringstream out;
    out << "{\"ok\":true,\"linked\":" << (linked ? "true" : "false")
        << ",\"oauth_linked\":" << (oauth_linked ? "true" : "false")
        << ",\"imap_linked\":" << (imap_linked ? "true" : "false")
        << ",\"auth_mode\":\"" << json_escape(config_.auth_mode) << "\",\"transport\":\""
        << transport << "\",\"link_pending\":" << (pending ? "true" : "false");
    if (!pending_user_code_.empty()) {
        out << ",\"user_code\":\"" << json_escape(pending_user_code_) << "\"";
    }
    if (!pending_verification_url_.empty()) {
        out << ",\"verification_url\":\"" << json_escape(pending_verification_url_) << "\"";
    }
    std::string access;
    std::string refresh;
    uint64_t expires = 0;
    std::string email;
    if (load_token(access, refresh, expires, email) && !email.empty()) {
        out << ",\"oauth_email\":\"" << json_escape(email) << "\"";
    }
    std::string imap_email;
    std::string imap_password;
    std::string host_override;
    if (load_imap_credentials(imap_email, imap_password, host_override) && !imap_email.empty()) {
        out << ",\"imap_email\":\"" << json_escape(imap_email) << "\"";
        out << ",\"email\":\"" << json_escape(imap_email) << "\"";
    } else if (!email.empty()) {
        out << ",\"email\":\"" << json_escape(email) << "\"";
    }
    out << "}";
    return out.str();
}

std::string GmailBackend::curl_post_form(const std::string &url,
                                         const std::string &form_body) const
{
    const std::string cmd =
        "curl -sS -X POST " + shell_escape(url) + " -H " +
        shell_escape("Content-Type: application/x-www-form-urlencoded") + " -d " +
        shell_escape(form_body) + " 2>/dev/null";
    return run_command(cmd);
}

std::string GmailBackend::curl_get_auth(const std::string &url,
                                        const std::string &access_token) const
{
    const std::string cmd =
        "curl -sS " + shell_escape(url) + " -H " +
        shell_escape("Authorization: Bearer " + access_token) + " -H " +
        shell_escape("User-Agent: " + config_.user_agent) + " 2>/dev/null";
    return run_command(cmd);
}

std::string GmailBackend::curl_post_auth(const std::string &url, const std::string &access_token,
                                         const std::string &body) const
{
    const std::string cmd =
        "curl -sS -X POST " + shell_escape(url) + " -H " +
        shell_escape("Authorization: Bearer " + access_token) + " -H " +
        shell_escape("Content-Type: application/json") + " -H " +
        shell_escape("User-Agent: " + config_.user_agent) + " -d " + shell_escape(body) +
        " 2>/dev/null";
    return run_command(cmd);
}

std::string GmailBackend::curl_post_auth_empty(const std::string &url,
                                               const std::string &access_token) const
{
    const std::string cmd =
        "curl -sS -X POST " + shell_escape(url) + " -H " +
        shell_escape("Authorization: Bearer " + access_token) + " -H " +
        shell_escape("User-Agent: " + config_.user_agent) + " 2>/dev/null";
    return run_command(cmd);
}

bool GmailBackend::ensure_access_token(std::string &access_token)
{
    std::string refresh;
    uint64_t expires_at = 0;
    std::string email;
    if (!load_token(access_token, refresh, expires_at, email)) {
        return false;
    }
    if (!access_token.empty() && expires_at > unix_now_sec() + 60) {
        return true;
    }
    const std::string client_id = load_client_id();
    if (client_id.empty()) {
        return false;
    }
    const std::string form = "client_id=" + url_encode_form(client_id) +
                             "&refresh_token=" + url_encode_form(refresh) +
                             "&grant_type=refresh_token";
    const std::string response = curl_post_form("https://oauth2.googleapis.com/token", form);
    const std::string new_access = json_get_string(response, "access_token");
    if (new_access.empty()) {
        return false;
    }
    const std::string expires_in = json_get_string(response, "expires_in");
    const uint64_t new_expires =
        unix_now_sec() + (expires_in.empty() ? 3600 : std::stoull(expires_in));
    save_token(new_access, refresh, new_expires, email);
    access_token = new_access;
    return true;
}

std::string GmailBackend::fetch_profile_email(const std::string &access_token)
{
    const std::string profile =
        curl_get_auth("https://gmail.googleapis.com/gmail/v1/users/me/profile", access_token);
    return json_get_string(profile, "emailAddress");
}

std::string GmailBackend::run_link_workflow()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }
    if (is_oauth_linked()) {
        std::string access;
        std::string refresh;
        uint64_t expires = 0;
        std::string email;
        load_token(access, refresh, expires, email);
        return "{\"ok\":true,\"linked\":true,\"email\":\"" + json_escape(email) + "\"}";
    }
    if (link_watch_active_.load()) {
        return link_status();
    }

    const std::string client_id = load_client_id();
    if (client_id.empty()) {
        return "{\"ok\":false,\"error\":\"Gmail client_id missing; run braillatron-install-gmail-oauth\"}";
    }

    link_watch_active_ = true;
    const std::string scope = url_encode_form(config_.scopes);
    const std::string form = "client_id=" + url_encode_form(client_id) + "&scope=" + scope;
    const std::string device_response =
        curl_post_form("https://oauth2.googleapis.com/device/code", form);

    const std::string device_code = json_get_string(device_response, "device_code");
    pending_user_code_ = json_get_string(device_response, "user_code");
    pending_verification_url_ = json_get_string(device_response, "verification_url");
    const std::string interval_str = json_get_string(device_response, "interval");
    const int interval = interval_str.empty() ? 5 : std::atoi(interval_str.c_str());

    if (device_code.empty() || pending_user_code_.empty()) {
        link_watch_active_ = false;
        const std::string error = json_get_string(device_response, "error");
        return "{\"ok\":false,\"error\":\"" +
               json_escape(error.empty() ? "device code request failed" : error) + "\"}";
    }

    if (events_ != nullptr) {
        events_->emit("gmail.link_pending",
                      "{\"user_code\":\"" + json_escape(pending_user_code_) +
                          "\",\"verification_url\":\"" +
                          json_escape(pending_verification_url_) + "\"}");
    }

    const uint32_t max_polls = config_.link_timeout_sec / static_cast<uint32_t>(interval > 0 ? interval : 5);
    for (uint32_t i = 0; i < max_polls; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(interval > 0 ? interval : 5));
        const std::string poll_form = "client_id=" + url_encode_form(client_id) +
                                      "&device_code=" + url_encode_form(device_code) +
                                      "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
        const std::string token_response =
            curl_post_form("https://oauth2.googleapis.com/token", poll_form);
        const std::string access = json_get_string(token_response, "access_token");
        if (!access.empty()) {
            const std::string refresh = json_get_string(token_response, "refresh_token");
            const std::string expires_in = json_get_string(token_response, "expires_in");
            const uint64_t expires =
                unix_now_sec() + (expires_in.empty() ? 3600 : std::stoull(expires_in));
            std::string email = fetch_profile_email(access);
            save_token(access, refresh, expires, email);
            run_command("chmod 700 " + shell_escape(config_.credentials_dir));
            run_command("chmod 600 " + shell_escape(config_.token_path));
            link_watch_active_ = false;
            if (events_ != nullptr) {
                events_->emit("gmail.link_completed",
                              "{\"email\":\"" + json_escape(email) + "\"}");
            }
            return "{\"ok\":true,\"linked\":true,\"email\":\"" + json_escape(email) +
                   "\",\"user_code\":\"" + json_escape(pending_user_code_) + "\"}";
        }
        const std::string error = json_get_string(token_response, "error");
        if (error != "authorization_pending" && error != "slow_down") {
            break;
        }
    }

    link_watch_active_ = false;
    if (events_ != nullptr) {
        events_->emit("gmail.link_failed", "{\"error\":\"link not completed\"}");
    }
    return "{\"ok\":false,\"error\":\"link not completed\",\"user_code\":\"" +
           json_escape(pending_user_code_) + "\",\"verification_url\":\"" +
           json_escape(pending_verification_url_) + "\"}";
}

std::string GmailBackend::base64url_encode(const std::string &input)
{
    return encode_base64url(input);
}

std::string GmailBackend::build_rfc2822(const std::string &to, const std::string &subject,
                                        const std::string &body)
{
    std::ostringstream out;
    out << "To: " << to << "\r\n";
    out << "Subject: " << subject << "\r\n";
    out << "Content-Type: text/plain; charset=UTF-8\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string GmailBackend::header_from_message(const std::string &message_json,
                                              const std::string &header_name)
{
    const std::string payload = find_json_object(message_json, "payload");
    if (payload.empty()) {
        return {};
    }
    const std::string headers_body = json_get_array_body(payload, "headers");
    for (const auto &header : json_split_objects("[" + headers_body + "]")) {
        const std::string name = json_get_string(header, "name");
        if (name == header_name) {
            return json_get_string(header, "value");
        }
    }
    return {};
}

std::string GmailBackend::extract_plain_body(const std::string &message_json)
{
    const std::string snippet = json_get_string(message_json, "snippet");
    const std::string payload = find_json_object(message_json, "payload");
    if (payload.empty()) {
        return snippet;
    }

    const std::string mime = json_get_string(payload, "mimeType");
    const std::string body_data = json_get_string(find_json_object(payload, "body"), "data");
    if (!body_data.empty() && mime == "text/plain") {
        const std::string decoded = decode_base64url(body_data);
        if (!decoded.empty()) {
            return decoded;
        }
    }

    const std::string parts_body = json_get_array_body(payload, "parts");
    for (const auto &part : json_split_objects("[" + parts_body + "]")) {
        const std::string part_mime = json_get_string(part, "mimeType");
        const std::string part_data =
            json_get_string(find_json_object(part, "body"), "data");
        if (part_mime == "text/plain" && !part_data.empty()) {
            const std::string decoded = decode_base64url(part_data);
            if (!decoded.empty()) {
                return decoded;
            }
        }
        const std::string nested_parts = json_get_array_body(part, "parts");
        for (const auto &nested : json_split_objects("[" + nested_parts + "]")) {
            if (json_get_string(nested, "mimeType") == "text/plain") {
                const std::string nested_data =
                    json_get_string(find_json_object(nested, "body"), "data");
                if (!nested_data.empty()) {
                    const std::string decoded = decode_base64url(nested_data);
                    if (!decoded.empty()) {
                        return decoded;
                    }
                }
            }
        }
    }
    return snippet;
}

std::string GmailBackend::format_inbox_entry(const std::string &message_json)
{
    const std::string id = json_get_string(message_json, "id");
    const std::string from = header_from_message(message_json, "From");
    const std::string subject = header_from_message(message_json, "Subject");
    const std::string snippet = json_get_string(message_json, "snippet");
    std::ostringstream out;
    out << "{\"id\":\"" << json_escape(id) << "\",\"from\":\"" << json_escape(from)
        << "\",\"subject\":\"" << json_escape(subject.empty() ? "(no subject)" : subject)
        << "\",\"snippet\":\"" << json_escape(snippet) << "\"}";
    return out.str();
}

std::vector<std::string> GmailBackend::format_message_brf_lines(const std::string &from,
                                                                const std::string &subject,
                                                                const std::string &body)
{
    std::vector<std::string> lines;
    lines.push_back("From: " + from);
    lines.push_back("Subject: " + (subject.empty() ? "(no subject)" : subject));
    lines.push_back("");

    std::string current;
    for (char ch : body) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

std::string GmailBackend::export_filename(const std::string &subject)
{
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                   std::chrono::system_clock::now().time_since_epoch())
                                                   .count());

    std::string stem;
    stem.reserve(40);
    for (unsigned char ch : subject) {
        if (std::isalnum(ch)) {
            stem.push_back(static_cast<char>(std::tolower(ch)));
        } else if ((ch == ' ' || ch == '-' || ch == '_') && !stem.empty() && stem.back() != '-') {
            stem.push_back('-');
        }
        if (stem.size() >= 32) {
            break;
        }
    }
    while (!stem.empty() && stem.back() == '-') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "message";
    }
    return stem + "-" + std::to_string(now) + ".brf";
}

std::string GmailBackend::list_inbox()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"gmail not linked\"}";
    }
    if (active_transport() == Transport::Imap) {
        return list_inbox_imap();
    }
    return list_inbox_oauth();
}

std::string GmailBackend::list_inbox_oauth()
{
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }

    const std::string list_url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages?maxResults=" +
        std::to_string(config_.inbox_limit) + "&labelIds=INBOX";
    const std::string list_response = curl_get_auth(list_url, access);
    const std::string messages_body = json_get_array_body(list_response, "messages");
    if (messages_body.empty()) {
        return "{\"ok\":true,\"messages\":[]}";
    }

    std::ostringstream out;
    out << "{\"ok\":true,\"messages\":[";
    bool first = true;
    for (const auto &ref : json_split_objects("[" + messages_body + "]")) {
        const std::string id = json_get_string(ref, "id");
        if (id.empty()) {
            continue;
        }
        const std::string detail_url =
            "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + id +
            "?format=metadata&metadataHeaders=From&metadataHeaders=Subject";
        const std::string detail = curl_get_auth(detail_url, access);
        if (!first) {
            out << ',';
        }
        first = false;
        out << format_inbox_entry(detail);
    }
    out << "]}";
    return out.str();
}

std::string GmailBackend::read_message(const std::string &message_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"gmail not linked\"}";
    }
    if (message_id.empty()) {
        return "{\"ok\":false,\"error\":\"missing message_id\"}";
    }
    if (active_transport() == Transport::Imap) {
        return read_message_imap(message_id);
    }
    return read_message_oauth(message_id);
}

std::string GmailBackend::read_message_oauth(const std::string &message_id)
{
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }

    const std::string url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + message_id + "?format=full";
    const std::string message = curl_get_auth(url, access);
    if (message.find("\"error\"") != std::string::npos) {
        return "{\"ok\":false,\"error\":\"message fetch failed\"}";
    }

    const std::string from = header_from_message(message, "From");
    const std::string subject = header_from_message(message, "Subject");
    const std::string body = extract_plain_body(message);
    const std::string thread_id = json_get_string(message, "threadId");
    std::ostringstream out;
    out << "{\"ok\":true,\"message\":{\"id\":\"" << json_escape(message_id) << "\",\"thread_id\":\""
        << json_escape(thread_id) << "\",\"from\":\"" << json_escape(from) << "\",\"subject\":\""
        << json_escape(subject) << "\",\"body\":\"" << json_escape(body) << "\"}}";
    return out.str();
}

std::string GmailBackend::send_message(const std::string &to, const std::string &subject,
                                       const std::string &body)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"gmail not linked\"}";
    }
    if (to.empty() || body.empty()) {
        return "{\"ok\":false,\"error\":\"missing to or body\"}";
    }
    if (active_transport() == Transport::Imap) {
        return "{\"ok\":false,\"error\":\"IMAP send requires SMTP setup (coming soon); use OAuth Gmail for send\"}";
    }
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }

    const std::string raw = base64url_encode(build_rfc2822(to, subject, body));
    const std::string payload = "{\"raw\":\"" + json_escape(raw) + "\"}";
    const std::string response = curl_post_auth(
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/send", access, payload);
    if (json_get_string(response, "id").empty()) {
        return "{\"ok\":false,\"error\":\"send failed\"}";
    }
    return "{\"ok\":true}";
}

std::string GmailBackend::reply_message(const std::string &message_id, const std::string &body)
{
    if (!config_.enabled || message_id.empty() || body.empty()) {
        return "{\"ok\":false,\"error\":\"invalid reply request\"}";
    }
    if (active_transport() == Transport::Imap) {
        return "{\"ok\":false,\"error\":\"IMAP reply requires SMTP setup (coming soon)\"}";
    }
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }

    const std::string url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + message_id + "?format=metadata";
    const std::string original = curl_get_auth(url, access);
    const std::string thread_id = json_get_string(original, "threadId");
    const std::string to = header_from_message(original, "From");
    const std::string subject = header_from_message(original, "Subject");
    const std::string reply_subject =
        subject.rfind("Re:", 0) == 0 ? subject : "Re: " + subject;

    const std::string raw =
        base64url_encode(build_rfc2822(to, reply_subject, body));
    std::ostringstream payload;
    payload << "{\"raw\":\"" << json_escape(raw) << "\"";
    if (!thread_id.empty()) {
        payload << ",\"threadId\":\"" << json_escape(thread_id) << "\"";
    }
    payload << "}";

    const std::string response = curl_post_auth(
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/send", access, payload.str());
    if (json_get_string(response, "id").empty()) {
        return "{\"ok\":false,\"error\":\"reply failed\"}";
    }
    return "{\"ok\":true}";
}

std::string GmailBackend::archive_message(const std::string &message_id)
{
    if (!config_.enabled || message_id.empty()) {
        return "{\"ok\":false,\"error\":\"invalid archive request\"}";
    }
    if (active_transport() == Transport::Imap) {
        return "{\"ok\":false,\"error\":\"IMAP archive not supported yet\"}";
    }
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }
    const std::string url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + message_id + "/modify";
    const std::string response =
        curl_post_auth(url, access, "{\"removeLabelIds\":[\"INBOX\"]}");
    if (response.find("\"error\"") != std::string::npos) {
        return "{\"ok\":false,\"error\":\"archive failed\"}";
    }
    return "{\"ok\":true}";
}

std::string GmailBackend::delete_message(const std::string &message_id)
{
    if (!config_.enabled || message_id.empty()) {
        return "{\"ok\":false,\"error\":\"invalid delete request\"}";
    }
    if (active_transport() == Transport::Imap) {
        std::string email;
        std::string password;
        std::string host_override;
        if (!load_imap_credentials(email, password, host_override)) {
            return "{\"ok\":false,\"error\":\"IMAP credentials missing\"}";
        }
        const std::string host = resolve_imap_host(email, host_override);
        const std::string response =
            imap_curl("INBOX", "UID STORE " + message_id + " +FLAGS (\\Deleted)", email, password,
                      host);
        (void)imap_curl("INBOX", "EXPUNGE", email, password, host);
        if (response.empty()) {
            return "{\"ok\":false,\"error\":\"IMAP delete failed\"}";
        }
        return "{\"ok\":true}";
    }
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }
    const std::string url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + message_id + "/trash";
    const std::string response = curl_post_auth_empty(url, access);
    if (response.find("\"error\"") != std::string::npos) {
        return "{\"ok\":false,\"error\":\"delete failed\"}";
    }
    return "{\"ok\":true}";
}

std::string GmailBackend::star_message(const std::string &message_id)
{
    if (!config_.enabled || message_id.empty()) {
        return "{\"ok\":false,\"error\":\"invalid star request\"}";
    }
    if (active_transport() == Transport::Imap) {
        std::string email;
        std::string password;
        std::string host_override;
        if (!load_imap_credentials(email, password, host_override)) {
            return "{\"ok\":false,\"error\":\"IMAP credentials missing\"}";
        }
        const std::string host = resolve_imap_host(email, host_override);
        const std::string response =
            imap_curl("INBOX", "UID STORE " + message_id + " +FLAGS (\\Flagged)", email, password,
                      host);
        if (response.empty()) {
            return "{\"ok\":false,\"error\":\"IMAP star failed\"}";
        }
        return "{\"ok\":true}";
    }
    std::string access;
    if (!ensure_access_token(access)) {
        return "{\"ok\":false,\"error\":\"gmail token unavailable\"}";
    }
    const std::string url =
        "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + message_id + "/modify";
    const std::string response =
        curl_post_auth(url, access, "{\"addLabelIds\":[\"STARRED\"]}");
    if (response.find("\"error\"") != std::string::npos) {
        return "{\"ok\":false,\"error\":\"star failed\"}";
    }
    return "{\"ok\":true}";
}

std::string GmailBackend::unlink()
{
    if (file_exists(config_.token_path)) {
        run_command("rm -f " + shell_escape(config_.token_path));
    }
    link_watch_active_ = false;
    pending_user_code_.clear();
    pending_verification_url_.clear();
    return "{\"ok\":true,\"linked\":" + std::string(is_linked() ? "true" : "false") +
           ",\"oauth_linked\":false,\"imap_linked\":" +
           std::string(is_imap_linked() ? "true" : "false") + "}";
}

std::string GmailBackend::unlink_imap()
{
    if (file_exists(config_.imap_password_path)) {
        run_command("rm -f " + shell_escape(config_.imap_password_path));
    }
    return "{\"ok\":true,\"linked\":" + std::string(is_linked() ? "true" : "false") +
           ",\"imap_linked\":false,\"oauth_linked\":" +
           std::string(is_oauth_linked() ? "true" : "false") + "}";
}

bool GmailBackend::load_imap_credentials(std::string &email, std::string &password,
                                         std::string &imap_host_override) const
{
    email.clear();
    password.clear();
    imap_host_override.clear();
    if (!file_exists(config_.imap_password_path)) {
        return false;
    }
    std::ifstream file(config_.imap_password_path);
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim_copy(line.substr(0, eq));
        const std::string value = trim_copy(line.substr(eq + 1));
        if (key == "email" || key == "username" || key == "user") {
            email = value;
        } else if (key == "password" || key == "app_password") {
            password = value;
        } else if (key == "imap_host" || key == "host") {
            imap_host_override = value;
        }
    }
    return !email.empty() && !password.empty();
}

std::string GmailBackend::resolve_imap_host(const std::string &email,
                                            const std::string &host_override) const
{
    if (!host_override.empty()) {
        return host_override;
    }
    if (!config_.imap_host.empty()) {
        return config_.imap_host;
    }
    const size_t at = email.find('@');
    if (at == std::string::npos || at + 1 >= email.size()) {
        return "imap.gmail.com";
    }
    const std::string domain = to_lower_copy(email.substr(at + 1));
    if (domain == "gmail.com" || domain == "googlemail.com") {
        return "imap.gmail.com";
    }
    if (domain == "outlook.com" || domain == "hotmail.com" || domain == "live.com" ||
        domain == "office365.com") {
        return "outlook.office365.com";
    }
    if (domain == "yahoo.com") {
        return "imap.mail.yahoo.com";
    }
    if (domain == "icloud.com" || domain == "me.com" || domain == "mac.com") {
        return "imap.mail.me.com";
    }
    // Many schools use Microsoft 365; default to Outlook host, override via imap.ini.
    return "outlook.office365.com";
}

std::string GmailBackend::imap_curl(const std::string &mailbox_path,
                                    const std::string &custom_request, const std::string &email,
                                    const std::string &password, const std::string &host) const
{
    std::ostringstream url;
    url << "imaps://" << host;
    if (config_.imap_port != 993) {
        url << ':' << config_.imap_port;
    }
    url << '/' << mailbox_path;
    std::ostringstream cmd;
    cmd << "curl -sS --connect-timeout 20 --max-time 90 --url " << shell_escape(url.str())
        << " --user " << shell_escape(email + ":" + password);
    if (!custom_request.empty()) {
        cmd << " -X " << shell_escape(custom_request);
    }
    cmd << " 2>/dev/null";
    return run_command(cmd.str());
}

bool GmailBackend::imap_login_ok(const std::string &email, const std::string &password,
                                 const std::string &host) const
{
    const std::string response = imap_curl("", "LIST \"\" INBOX", email, password, host);
    if (response.empty()) {
        return false;
    }
    const std::string lower = to_lower_copy(response);
    if (lower.find("authentication failed") != std::string::npos ||
        lower.find("invalid credentials") != std::string::npos ||
        lower.find("login failed") != std::string::npos ||
        lower.find("access denied") != std::string::npos ||
        (lower.find("authenti") != std::string::npos &&
         lower.find("fail") != std::string::npos)) {
        return false;
    }
    return response.find("INBOX") != std::string::npos ||
           response.find("* LIST") != std::string::npos ||
           lower.find(" ok ") != std::string::npos || lower.find("\nok ") != std::string::npos;
}

bool GmailBackend::import_imap_credentials_from_incoming()
{
    const std::vector<std::string> candidates = {
        "/data/braillatron/credentials/incoming/" + config_.imap_incoming_name,
        "/data/braillatron/credentials/incoming/gmail-imap.ini",
        "/data/braillatron/credentials/incoming/school-email.ini",
        "/data/braillatron/credentials/incoming/imap.ini",
    };
    for (const std::string &incoming : candidates) {
        if (!file_exists(incoming)) {
            continue;
        }
        ensure_directory(config_.credentials_dir);
        const std::string tmp = config_.imap_password_path + ".tmp";
        if (!atomic_move_file(incoming, tmp) && !atomic_move_file(incoming, config_.imap_password_path)) {
            // Fall back to copy if move across mount fails.
            run_command("cp " + shell_escape(incoming) + " " + shell_escape(tmp));
            run_command("rm -f " + shell_escape(incoming));
        }
        if (file_exists(tmp)) {
            run_command("chmod 600 " + shell_escape(tmp));
            if (!atomic_move_file(tmp, config_.imap_password_path)) {
                run_command("mv -f " + shell_escape(tmp) + " " +
                            shell_escape(config_.imap_password_path));
            }
        }
        run_command("chmod 700 " + shell_escape(config_.credentials_dir));
        run_command("chmod 600 " + shell_escape(config_.imap_password_path));
        return file_exists(config_.imap_password_path);
    }
    return false;
}

std::string GmailBackend::run_imap_link_workflow()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"gmail disabled\"}";
    }

    import_imap_credentials_from_incoming();

    std::string email;
    std::string password;
    std::string host_override;
    if (!load_imap_credentials(email, password, host_override)) {
        std::ostringstream out;
        out << "{\"ok\":true,\"linked\":false,\"imap_linked\":false,"
               "\"needs_credentials\":true,"
               "\"instructions\":\"Create imap.ini with email= and password= (app password). "
               "Send via LocalSend or copy to /data/braillatron/credentials/incoming/imap.ini, "
               "then choose Link IMAP email again. Optional imap_host= for non-Outlook schools.\"}";
        return out.str();
    }

    const std::string host = resolve_imap_host(email, host_override);
    if (!imap_login_ok(email, password, host)) {
        if (events_ != nullptr) {
            events_->emit("gmail.imap_link_failed",
                          "{\"error\":\"IMAP login failed\",\"email\":\"" + json_escape(email) +
                              "\"}");
        }
        return "{\"ok\":false,\"error\":\"IMAP login failed\",\"email\":\"" + json_escape(email) +
               "\",\"imap_host\":\"" + json_escape(host) + "\"}";
    }

    if (events_ != nullptr) {
        events_->emit("gmail.imap_link_completed",
                      "{\"email\":\"" + json_escape(email) + "\",\"imap_host\":\"" +
                          json_escape(host) + "\"}");
    }
    return "{\"ok\":true,\"linked\":true,\"imap_linked\":true,\"email\":\"" + json_escape(email) +
           "\",\"imap_host\":\"" + json_escape(host) + "\"}";
}

std::string GmailBackend::list_inbox_imap()
{
    std::string email;
    std::string password;
    std::string host_override;
    if (!load_imap_credentials(email, password, host_override)) {
        return "{\"ok\":false,\"error\":\"IMAP credentials missing\"}";
    }
    const std::string host = resolve_imap_host(email, host_override);
    const std::string search = imap_curl("INBOX", "UID SEARCH ALL", email, password, host);
    auto uids = parse_imap_search_uids(search);
    if (uids.empty()) {
        return "{\"ok\":true,\"messages\":[],\"transport\":\"imap\"}";
    }

    const size_t limit = static_cast<size_t>(config_.inbox_limit);
    if (uids.size() > limit) {
        uids.erase(uids.begin(), uids.end() - static_cast<std::ptrdiff_t>(limit));
    }
    std::reverse(uids.begin(), uids.end()); // newest first when UIDs ascend

    std::ostringstream out;
    out << "{\"ok\":true,\"transport\":\"imap\",\"messages\":[";
    bool first = true;
    for (const std::string &uid : uids) {
        const std::string raw =
            imap_curl("INBOX/;UID=" + uid, "", email, password, host);
        if (raw.empty()) {
            continue;
        }
        const std::string from = header_value_from_rfc822(raw, "From");
        const std::string subject = header_value_from_rfc822(raw, "Subject");
        std::string snippet = plain_body_from_rfc822(raw);
        if (snippet.size() > 160) {
            snippet = snippet.substr(0, 160);
        }
        for (char &ch : snippet) {
            if (ch == '\n' || ch == '\r') {
                ch = ' ';
            }
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"id\":\"" << json_escape(uid) << "\",\"from\":\"" << json_escape(from)
            << "\",\"subject\":\"" << json_escape(subject.empty() ? "(no subject)" : subject)
            << "\",\"snippet\":\"" << json_escape(snippet) << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string GmailBackend::read_message_imap(const std::string &message_id)
{
    std::string email;
    std::string password;
    std::string host_override;
    if (!load_imap_credentials(email, password, host_override)) {
        return "{\"ok\":false,\"error\":\"IMAP credentials missing\"}";
    }
    const std::string host = resolve_imap_host(email, host_override);
    const std::string raw =
        imap_curl("INBOX/;UID=" + message_id, "", email, password, host);
    if (raw.empty()) {
        return "{\"ok\":false,\"error\":\"IMAP message fetch failed\"}";
    }
    const std::string from = header_value_from_rfc822(raw, "From");
    const std::string subject = header_value_from_rfc822(raw, "Subject");
    const std::string body = plain_body_from_rfc822(raw);
    std::ostringstream out;
    out << "{\"ok\":true,\"transport\":\"imap\",\"message\":{\"id\":\"" << json_escape(message_id)
        << "\",\"thread_id\":\"\",\"from\":\"" << json_escape(from) << "\",\"subject\":\""
        << json_escape(subject) << "\",\"body\":\"" << json_escape(body) << "\"}}";
    return out.str();
}

} // namespace braillatron::connect
