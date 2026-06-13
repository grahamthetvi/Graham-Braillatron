#pragma once

#include "connect_config.h"
#include "event_writer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::connect {

class GmailBackend {
public:
    GmailBackend(GmailConfig config, EventWriter *events);

    bool is_linked() const;
    std::string link_status() const;
    std::string run_link_workflow();
    std::string list_inbox();
    std::string read_message(const std::string &message_id);
    std::string send_message(const std::string &to, const std::string &subject,
                             const std::string &body);
    std::string reply_message(const std::string &message_id, const std::string &body);
    std::string archive_message(const std::string &message_id);
    std::string delete_message(const std::string &message_id);
    std::string star_message(const std::string &message_id);
    std::string unlink();

    static std::string base64url_encode(const std::string &input);
    static std::string build_rfc2822(const std::string &to, const std::string &subject,
                                     const std::string &body);
    static std::string header_from_message(const std::string &message_json,
                                           const std::string &header_name);
    static std::string extract_plain_body(const std::string &message_json);
    static std::string format_inbox_entry(const std::string &message_json);
    static std::vector<std::string> format_message_brf_lines(const std::string &from,
                                                             const std::string &subject,
                                                             const std::string &body);
    static std::string export_filename(const std::string &subject);

private:
    std::string load_client_id() const;
    bool load_token(std::string &access_token, std::string &refresh_token,
                    uint64_t &expires_at, std::string &email) const;
    bool save_token(const std::string &access_token, const std::string &refresh_token,
                    uint64_t expires_at, const std::string &email) const;
    bool ensure_access_token(std::string &access_token);
    std::string curl_post_form(const std::string &url, const std::string &form_body) const;
    std::string curl_get_auth(const std::string &url, const std::string &access_token) const;
    std::string curl_post_auth(const std::string &url, const std::string &access_token,
                               const std::string &body) const;
    std::string curl_post_auth_empty(const std::string &url,
                                     const std::string &access_token) const;
    std::string fetch_profile_email(const std::string &access_token);

    GmailConfig config_;
    EventWriter *events_;
    std::atomic<bool> link_watch_active_ {false};
    std::string pending_user_code_;
    std::string pending_verification_url_;
};

} // namespace braillatron::connect
