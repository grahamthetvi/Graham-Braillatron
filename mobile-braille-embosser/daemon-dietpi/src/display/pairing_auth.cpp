#include "pairing_auth.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace braillatron::display {

namespace {

constexpr int kMaxPairingFailures = 5;
constexpr auto kPairingLockDuration = std::chrono::minutes(10);
constexpr auto kPairingCodeLifetime = std::chrono::minutes(5);

// Minimal SHA-256 (public-domain style) for pairing code hashing.
struct Sha256 {
    static constexpr size_t kDigestSize = 32;

    void update(const uint8_t *data, size_t len)
    {
        for (size_t i = 0; i < len; ++i) {
            buffer_[buffer_len_++] = data[i];
            if (buffer_len_ == 64) {
                transform();
                buffer_len_ = 0;
            }
        }
    }

    void update(const std::string &text)
    {
        update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    }

    std::array<uint8_t, kDigestSize> finalize()
    {
        const uint64_t bit_len = total_len_ * 8 + buffer_len_ * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buffer_len_ != 56) {
            update(&zero, 1);
        }
        for (int i = 7; i >= 0; --i) {
            const uint8_t b = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF);
            update(&b, 1);
        }
        transform();

        std::array<uint8_t, kDigestSize> digest {};
        for (size_t i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
        }
        return digest;
    }

private:
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    static uint32_t sig0(uint32_t x)
    {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }
    static uint32_t sig1(uint32_t x)
    {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }
    static uint32_t gam0(uint32_t x)
    {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }
    static uint32_t gam1(uint32_t x)
    {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }

    void transform()
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        uint32_t w[64];
        for (size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(buffer_[i * 4]) << 24) |
                   (static_cast<uint32_t>(buffer_[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(buffer_[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(buffer_[i * 4 + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            w[i] = gam1(w[i - 2]) + w[i - 7] + gam0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];

        for (size_t i = 0; i < 64; ++i) {
            const uint32_t t1 = h + sig1(e) + ch(e, f, g) + k[i] + w[i];
            const uint32_t t2 = sig0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
        total_len_ += 64;
    }

    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buffer_[64] {};
    size_t buffer_len_ = 0;
    uint64_t total_len_ = 0;
};

std::string digest_hex(const std::array<uint8_t, Sha256::kDigestSize> &digest)
{
    std::ostringstream stream;
    for (uint8_t byte : digest) {
        stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return stream.str();
}

std::mt19937 &rng()
{
    static std::random_device seed;
    static std::mt19937 engine(seed());
    return engine;
}

} // namespace

PairingAuth::PairingAuth(uint32_t session_idle_minutes)
    : session_idle_minutes_(session_idle_minutes)
{
}

std::string PairingAuth::hash_code(const std::string &code) const
{
    Sha256 sha;
    sha.update("braillatron-remote-display-v1:");
    sha.update(code);
    return digest_hex(sha.finalize());
}

void PairingAuth::set_active_pairing_hash(const std::string &hash,
                                          std::chrono::steady_clock::time_point expires_at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_pairing_hash_ = hash;
    pairing_expires_at_ = expires_at;
    pairing_active_ = !hash.empty();
    pairing_failures_ = 0;
    pairing_lock_until_ = {};
}

void PairingAuth::clear_active_pairing()
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_pairing_hash_.clear();
    pairing_active_ = false;
}

bool PairingAuth::verify_pairing_code(const std::string &code) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pairing_lock_until_ > std::chrono::steady_clock::now()) {
        return false;
    }
    if (!pairing_active_ || pairing_expires_at_ < std::chrono::steady_clock::now()) {
        return false;
    }
    return hash_code(code) == active_pairing_hash_;
}

bool PairingAuth::is_pairing_locked() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pairing_lock_until_ > std::chrono::steady_clock::now();
}

void PairingAuth::record_pairing_failure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++pairing_failures_;
    if (pairing_failures_ >= kMaxPairingFailures) {
        pairing_lock_until_ = std::chrono::steady_clock::now() + kPairingLockDuration;
        pairing_failures_ = 0;
    }
}

std::string PairingAuth::create_session()
{
    const std::string token = generate_session_token();
    const auto expires = std::chrono::steady_clock::now() +
                         std::chrono::minutes(session_idle_minutes_);
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = expires;
    return token;
}

bool PairingAuth::validate_session(const std::string &token) const
{
    if (token.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        return false;
    }
    if (it->second < std::chrono::steady_clock::now()) {
        sessions_.erase(it);
        return false;
    }
    sessions_[token] = std::chrono::steady_clock::now() +
                       std::chrono::minutes(session_idle_minutes_);
    return true;
}

void PairingAuth::revoke_session(const std::string &token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}

void PairingAuth::prune_expired_sessions()
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second < now) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t PairingAuth::active_session_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

std::string generate_pairing_code()
{
    std::uniform_int_distribution<int> dist(0, 999999);
    const int code = dist(rng());
    char buffer[7];
    std::snprintf(buffer, sizeof(buffer), "%06d", code);
    return buffer;
}

std::string generate_session_token()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<size_t> dist(0, sizeof(alphabet) - 2);
    std::string token;
    token.reserve(32);
    for (int i = 0; i < 32; ++i) {
        token.push_back(alphabet[dist(rng())]);
    }
    return token;
}

} // namespace braillatron::display
