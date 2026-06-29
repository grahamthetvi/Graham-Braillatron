#include "pairing_auth.h"

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace braillatron::display {

namespace {

void sha256_transform(const uint8_t *chunk, uint32_t *state)
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
               (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = ((w[i - 15] >> 7) | (w[i - 15] << 25)) ^
                              ((w[i - 15] >> 18) | (w[i - 15] << 14)) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ((w[i - 2] >> 17) | (w[i - 2] << 15)) ^
                              ((w[i - 2] >> 19) | (w[i - 2] << 13)) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        const uint32_t s0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::string sha256_hex(const std::string &input)
{
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const size_t bit_len = input.size() * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        sha256_transform(msg.data() + offset, state);
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint32_t word : state) {
        out << std::setw(8) << word;
    }
    return out.str();
}

} // namespace

PairingAuth::PairingAuth(uint32_t session_idle_minutes)
    : session_idle_minutes_(session_idle_minutes)
{
}

std::string PairingAuth::hash_code(const std::string &code)
{
    return sha256_hex(code);
}

std::string PairingAuth::generate_session_token()
{
    static std::mt19937 rng {std::random_device {}()};
    static std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream token;
    for (int i = 0; i < 32; ++i) {
        token << std::hex << dist(rng);
    }
    return token.str();
}

std::string PairingAuth::generate_pairing_code()
{
    static std::mt19937 rng {std::random_device {}()};
    std::uniform_int_distribution<int> dist(0, 999999);
    char buffer[7];
    std::snprintf(buffer, sizeof(buffer), "%06d", dist(rng));
    return buffer;
}

std::string PairingAuth::start_pairing()
{
    // Fresh admin pairing clears prior lockout from failed browser attempts.
    reset_failures();
    active_pairing_code_ = generate_pairing_code();
    pairing_active_ = true;
    pairing_expires_ = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    return active_pairing_code_;
}

void PairingAuth::stop_pairing()
{
    pairing_active_ = false;
    active_pairing_code_.clear();
}

std::optional<std::string> PairingAuth::verify_pairing(const std::string &code)
{
    last_verify_error_.clear();
    if (rate_limited_) {
        if (std::chrono::steady_clock::now() < rate_limit_until_) {
            last_verify_error_ = "rate_limited";
            return std::nullopt;
        }
        rate_limited_ = false;
        failure_count_ = 0;
    }

    const std::string trimmed = code.size() >= 6 ? code.substr(0, 6) : code;
    const std::string hash = hash_code(trimmed);
    const bool matches_active =
        pairing_active_ && std::chrono::steady_clock::now() < pairing_expires_ &&
        hash == hash_code(active_pairing_code_);
    const bool matches_stored =
        !pairing_code_hash_.empty() && hash == pairing_code_hash_;

    if (!matches_active && !matches_stored) {
        if (pairing_active_ && std::chrono::steady_clock::now() >= pairing_expires_) {
            last_verify_error_ = "expired";
        } else {
            last_verify_error_ = "invalid_code";
        }
        register_failure();
        return std::nullopt;
    }

    reset_failures();
    const std::string token = generate_session_token();
    sessions_[token] = SessionEntry {std::chrono::steady_clock::now()};
    return token;
}

std::optional<std::string> PairingAuth::validate_session(const std::string &token)
{
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    const auto idle_limit = std::chrono::minutes(session_idle_minutes_);
    if (std::chrono::steady_clock::now() - it->second.last_seen > idle_limit) {
        sessions_.erase(it);
        return std::nullopt;
    }
    it->second.last_seen = std::chrono::steady_clock::now();
    return token;
}

void PairingAuth::revoke_session(const std::string &token)
{
    sessions_.erase(token);
}

void PairingAuth::clear_sessions()
{
    sessions_.clear();
}

void PairingAuth::register_failure()
{
    ++failure_count_;
    if (failure_count_ >= 5) {
        rate_limited_ = true;
        rate_limit_until_ = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    }
}

void PairingAuth::reset_failures()
{
    failure_count_ = 0;
    rate_limited_ = false;
}

} // namespace braillatron::display
