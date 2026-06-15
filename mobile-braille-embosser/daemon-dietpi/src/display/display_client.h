#pragma once

#include <string>

namespace braillatron::display {

class DisplayClient {
public:
    explicit DisplayClient(std::string socket_path);

    std::string request(const std::string &cmd, const std::string &extra_fields = "");
    bool ping();

private:
    std::string request_with_payload(const std::string &payload);

    std::string socket_path_;
};

} // namespace braillatron::display
