#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace braillatron::connect {

struct ConnectEvent {
    std::string type;
    std::string data_json;
};

class ConnectClient {
public:
    using AsyncCallback = std::function<void(const std::string &response_json)>;

    explicit ConnectClient(std::string socket_path = "/run/braillatron/connect.sock",
                           std::string event_path = "/run/braillatron/connect.events");

    bool ping();
    std::string request(const std::string &cmd, const std::string &extra_fields = {});
    void request_async(const std::string &cmd, const std::string &extra_fields,
                       AsyncCallback callback);
    void poll_events(const std::function<void(const ConnectEvent &)> &handler);
    void dispatch_async_response(const ConnectEvent &event);
    bool is_connected() const { return connected_; }

    // Used by connect_client_self_test.cpp to verify response correlation.
    void register_pending_for_test(const std::string &request_id, AsyncCallback callback);

    const std::string &socket_path() const { return socket_path_; }
    const std::string &event_path() const { return event_path_; }

private:
    std::string request_with_payload(const std::string &payload);

    std::string socket_path_;
    std::string event_path_;
    size_t event_offset_ = 0;
    bool connected_ = false;
    std::unordered_map<std::string, AsyncCallback> pending_;
};

} // namespace braillatron::connect
