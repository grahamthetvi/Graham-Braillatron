#pragma once

#include <functional>
#include <string>
#include <vector>

namespace braillatron::connect {

struct ConnectEvent {
    std::string type;
    std::string data_json;
};

class ConnectClient {
public:
    explicit ConnectClient(std::string socket_path = "/run/braillatron/connect.sock",
                           std::string event_path = "/run/braillatron/connect.events");

    bool ping();
    std::string request(const std::string &cmd, const std::string &extra_fields = {});
    void poll_events(const std::function<void(const ConnectEvent &)> &handler);
    bool is_connected() const { return connected_; }

    const std::string &socket_path() const { return socket_path_; }
    const std::string &event_path() const { return event_path_; }

private:
    std::string socket_path_;
    std::string event_path_;
    size_t event_offset_ = 0;
    bool connected_ = false;
};

} // namespace braillatron::connect
