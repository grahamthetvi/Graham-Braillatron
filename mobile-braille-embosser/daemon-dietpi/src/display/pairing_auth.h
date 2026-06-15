#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace braillatron::display {

class PairingAuth {
public:
    explicit PairingAuth(uint32_t session_idle_minutes = 30);

    std::string hash_code(const std::string &code) const;
    void set_active_pairing_hash(const std::string &hash,
                                 std::chrono::steady_clock::time_point expires_at);
    void clear_active_pairing();
    bool verify_pairing_code(const std::string &code) const;
    bool is_pairing_locked() const;
    void record_pairing_failure();
    std::string create_session();
    bool validate_session(const std::string &token) const;
    void revoke_session(const std::string &token);
    void prune_expired_sessions();
    size_t active_session_count() const;

private:
    uint32_t session_idle_minutes_;
    mutable std::mutex mutex_;
    std::string active_pairing_hash_;
    std::chrono::steady_clock::time_point pairing_expires_at_ {};
    bool pairing_active_ = false;
    int pairing_failures_ = 0;
    std::chrono::steady_clock::time_point pairing_lock_until_ {};
    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> sessions_;
};

std::string generate_pairing_code();
std::string generate_session_token();

} // namespace braillatron::display
