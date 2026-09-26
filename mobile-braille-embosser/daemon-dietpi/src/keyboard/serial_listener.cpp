#include "serial_listener.h"

extern "C" {
#include "protocol.h"
}

#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <unistd.h>
#include <vector>

namespace braillatron::keyboard {

namespace {

enum class ParseState {
    Sync,
    Header,
    Payload,
};

bool opcode_accepted(uint8_t opcode, size_t payload_size)
{
    switch (opcode) {
    case BRAILLATRON_OP_KEYBOARD_MATRIX:
        return payload_size == sizeof(braillatron_keyboard_matrix_t);
    case BRAILLATRON_OP_CHORD:
        return payload_size == sizeof(braillatron_chord_event_t);
    case BRAILLATRON_OP_SAFETY:
        return payload_size == sizeof(braillatron_safety_broadcast_t);
    default:
        return false;
    }
}

class FrameParser {
public:
    void reset()
    {
        state_ = ParseState::Sync;
        header_len_ = 0;
        payload_len_ = 0;
        expected_payload_ = 0;
    }

    std::optional<SerialFrame> push_byte(uint8_t byte)
    {
        switch (state_) {
        case ParseState::Sync:
            if (byte != BRAILLATRON_SYNC_BYTE) {
                return std::nullopt;
            }
            buffer_[0] = byte;
            header_len_ = 1;
            state_ = ParseState::Header;
            return std::nullopt;

        case ParseState::Header:
            buffer_[header_len_++] = byte;
            if (header_len_ < BRAILLATRON_FRAME_HEADER_SIZE) {
                return std::nullopt;
            }

            if (buffer_[1] != BRAILLATRON_PROTOCOL_VERSION) {
                reset();
                return std::nullopt;
            }

            expected_payload_ = buffer_[4];
            if (expected_payload_ > BRAILLATRON_FRAME_MAX_PAYLOAD) {
                reset();
                return std::nullopt;
            }

            payload_len_ = 0;
            state_ = ParseState::Payload;
            if (expected_payload_ == 0) {
                return finalize_frame();
            }
            return std::nullopt;

        case ParseState::Payload:
            buffer_[BRAILLATRON_FRAME_HEADER_SIZE + payload_len_++] = byte;
            if (payload_len_ < expected_payload_ + BRAILLATRON_FRAME_CRC_SIZE) {
                return std::nullopt;
            }
            return finalize_frame();
        }

        return std::nullopt;
    }

private:
    std::optional<SerialFrame> finalize_frame()
    {
        const size_t frame_len =
            BRAILLATRON_FRAME_HEADER_SIZE + expected_payload_ + BRAILLATRON_FRAME_CRC_SIZE;
        const uint16_t expected_crc =
            braillatron_crc16(buffer_, frame_len - BRAILLATRON_FRAME_CRC_SIZE);
        const uint16_t received_crc =
            static_cast<uint16_t>(buffer_[frame_len - 2]) |
            (static_cast<uint16_t>(buffer_[frame_len - 1]) << 8);

        const uint8_t opcode = buffer_[2];
        const size_t payload_size = expected_payload_;
        reset();

        if (expected_crc != received_crc) {
            return std::nullopt;
        }

        if (!opcode_accepted(opcode, payload_size)) {
            return std::nullopt;
        }

        SerialFrame frame;
        frame.opcode = opcode;
        frame.payload_len = payload_size;
        std::memcpy(frame.payload.data(), buffer_ + BRAILLATRON_FRAME_HEADER_SIZE,
                    payload_size);
        return frame;
    }

    ParseState state_ = ParseState::Sync;
    uint8_t buffer_[BRAILLATRON_FRAME_MAX_SIZE] {};
    size_t header_len_ = 0;
    size_t payload_len_ = 0;
    size_t expected_payload_ = 0;
};

} // namespace

SerialListener::SerialListener(platform::SerialLink *link)
    : link_(link)
{
}

SerialListener::~SerialListener()
{
    stop();
}

bool SerialListener::is_connected() const
{
    return connected_.load();
}

void SerialListener::set_disconnect_handler(SerialDisconnectHandler handler)
{
    disconnect_handler_ = std::move(handler);
}

bool SerialListener::start(FrameHandler handler)
{
    if (running_.load()) {
        return connected_.load();
    }

    handler_ = std::move(handler);
    if (link_ == nullptr || !link_->try_open()) {
        connected_ = false;
        return false;
    }

    running_ = true;
    connected_ = true;
    worker_ = std::thread([this]() {
        FrameParser parser;
        std::vector<uint8_t> chunk(256);
        bool disconnect_reported = false;

        while (running_.load()) {
            const int poll_result = link_->poll_readable(100);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            if (poll_result == 0) {
                continue;
            }

            const ssize_t nbytes = link_->read_bytes(chunk.data(), chunk.size());
            if (nbytes < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                }
                break;
            }
            if (nbytes == 0) {
                break;
            }

            for (ssize_t i = 0; i < nbytes; ++i) {
                if (auto frame = parser.push_byte(chunk[static_cast<size_t>(i)])) {
                    if (handler_) {
                        handler_(*frame);
                    }
                }
            }
        }

        connected_ = false;

        if (!disconnect_reported) {
            disconnect_reported = true;
            const std::string path = (link_ != nullptr) ? link_->device_path() : std::string();
            std::cerr << "[serial] disconnected from " << path << "\n";
            if (disconnect_handler_) {
                disconnect_handler_();
            }
        }
    });

    return true;
}

bool SerialListener::try_reconnect()
{
    if (connected_.load()) {
        return true;
    }

    stop();
    return start(handler_);
}

void SerialListener::stop()
{
    if (!running_.load() && !worker_.joinable()) {
        return;
    }

    running_ = false;

    if (worker_.joinable()) {
        worker_.join();
    }

    connected_ = false;
}

} // namespace braillatron::keyboard
