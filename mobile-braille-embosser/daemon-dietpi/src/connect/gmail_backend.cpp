#include "gmail_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

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

bool GmailBackend::is_linked() const
{
    std::string access;
    std::string refresh;
    uint64_t expires = 0;
    std::string email;
    return load_token(access, refresh, expires, email) && !refresh.empty();
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
    const bool linked = is_linked();
    const bool pending = link_watch_active_.load();
    std::ostringstream out;
    out << "{\"ok\":true,\"linked\":" << (linked ? "true" : "false") << ",\"link_pending\":"
        << (pending ? "true" : "false");
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
    if (is_linked()) {
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
    return "{\"ok\":true,\"linked\":false}";
}

} // namespace braillatron::connect
