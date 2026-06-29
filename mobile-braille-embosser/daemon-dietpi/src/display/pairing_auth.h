#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace braillatron::display {

class PairingAuth {
public:
    explicit PairingAuth(uint32_t session_idle_minutes = 30);

    void set_pairing_code_hash(std::string hash) { pairing_code_hash_ = std::move(hash); }
    const std::string &pairing_code_hash() const { return pairing_code_hash_; }

    std::string start_pairing();
    void stop_pairing();
    bool pairing_active() const { return pairing_active_; }
    const std::string &active_pairing_code() const { return active_pairing_code_; }

    std::optional<std::string> verify_pairing(const std::string &code);
    const std::string &last_verify_error() const { return last_verify_error_; }
    std::optional<std::string> validate_session(const std::string &token);
    void revoke_session(const std::string &token);
    void clear_sessions();

    bool rate_limited() const { return rate_limited_; }

    static std::string hash_code(const std::string &code);
    static std::string generate_session_token();
    static std::string generate_pairing_code();

private:
    void register_failure();
    void reset_failures();

    std::string pairing_code_hash_;
    std::string active_pairing_code_;
    bool pairing_active_ = false;
    std::chrono::steady_clock::time_point pairing_expires_ {};
    uint32_t session_idle_minutes_ = 30;
    int failure_count_ = 0;
    std::string last_verify_error_;
    bool rate_limited_ = false;
    std::chrono::steady_clock::time_point rate_limit_until_ {};
    struct SessionEntry {
        std::chrono::steady_clock::time_point last_seen {};
    };
    std::unordered_map<std::string, SessionEntry> sessions_;
};

} // namespace braillatron::display
