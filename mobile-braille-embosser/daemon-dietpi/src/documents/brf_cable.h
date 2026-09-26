#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace braillatron::documents {

struct BrfCableJob {
    std::string filename;
    std::string brf;
    bool accepted = false;
};

/**
 * Byte parser for a computer plugged into a free Pi USB-A port through a
 * USB-serial cable (115200 8N1). A job is North American BRF. It ends on a
 * form feed, which is what Graham Braille Editor's generic text embosser
 * writes, or after a quiet gap when the sender does not send a form feed.
 *
 * Optional first line: `BRF1 filename.brf`
 */
class BrfCableParser {
public:
    static constexpr uint64_t kIdleCompleteMs = 1500;
    static constexpr size_t kMaxBytes = 2u * 1024u * 1024u;

    std::optional<BrfCableJob> feed(const uint8_t *data, size_t len, uint64_t now_ms);
    std::optional<BrfCableJob> poll(uint64_t now_ms);

private:
    std::optional<BrfCableJob> finish(bool at_form_feed);
    BrfCableJob parse_buffer(std::string body) const;

    std::string buffer_;
    uint64_t last_byte_ms_ = 0;
    bool saw_byte_ = false;
};

/**
 * Opens one USB-serial device on the Pi. `device_spec` is `auto` (first
 * /dev/ttyUSB* that is not the Arduino CDC port) or an explicit path.
 */
class BrfCableReceiver {
public:
    BrfCableReceiver(std::string device_spec, uint32_t baud_rate, std::string avoid_device);

    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }
    const std::string &open_path() const { return open_path_; }

    std::vector<BrfCableJob> poll(uint64_t now_ms);

private:
    bool try_open(uint64_t now_ms);
    std::string resolve_device() const;

    std::string device_spec_;
    uint32_t baud_rate_ = 115200;
    std::string avoid_device_;
    bool enabled_ = true;
    int fd_ = -1;
    std::string open_path_;
    uint64_t next_open_attempt_ms_ = 0;
    bool logged_open_ = false;
    BrfCableParser parser_;
};

} // namespace braillatron::documents
