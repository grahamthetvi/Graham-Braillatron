#pragma once

#include <mutex>
#include <string>

namespace braillatron::connect {

class EventWriter {
public:
    explicit EventWriter(std::string path);

    void emit(const std::string &event_type, const std::string &payload_json);

private:
    std::string path_;
    std::mutex mutex_;
};

} // namespace braillatron::connect
