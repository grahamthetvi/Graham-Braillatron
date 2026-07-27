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
    bool is_oauth_linked() const;
    bool is_imap_linked() const;
    std::string link_status() const;
    std::string run_link_workflow();
    std::string run_imap_link_workflow();
    std::string list_inbox();
    std::string read_message(const std::string &message_id);
    std::string send_message(const std::string &to, const std::string &subject,
                             const std::string &body);
    std::string reply_message(const std::string &message_id, const std::string &body);
    std::string archive_message(const std::string &message_id);
    std::string delete_message(const std::string &message_id);
    std::string star_message(const std::string &message_id);
    std::string unlink();
    std::string unlink_imap();

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
    enum class Transport { Oauth, Imap };

    Transport active_transport() const;
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

    bool load_imap_credentials(std::string &email, std::string &password,
                               std::string &imap_host_override) const;
    std::string resolve_imap_host(const std::string &email,
                                  const std::string &host_override) const;
    std::string imap_curl(const std::string &mailbox_path, const std::string &custom_request,
                          const std::string &email, const std::string &password,
                          const std::string &host) const;
    bool imap_login_ok(const std::string &email, const std::string &password,
                       const std::string &host) const;
    std::string list_inbox_imap();
    std::string read_message_imap(const std::string &message_id);
    std::string list_inbox_oauth();
    std::string read_message_oauth(const std::string &message_id);
    bool import_imap_credentials_from_incoming();

    GmailConfig config_;
    EventWriter *events_;
    std::atomic<bool> link_watch_active_ {false};
    std::string pending_user_code_;
    std::string pending_verification_url_;
};

} // namespace braillatron::connect
