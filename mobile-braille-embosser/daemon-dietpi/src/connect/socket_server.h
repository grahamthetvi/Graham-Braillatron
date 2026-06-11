#pragma once

#include <functional>
#include <string>

namespace braillatron::connect {

class SocketServer {
public:
    explicit SocketServer(std::string path);
    ~SocketServer();

    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;

    bool listen();
    void poll_once(const std::function<std::string(const std::string &)> &handler);
    void close();

private:
    std::string path_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
};

} // namespace braillatron::connect
