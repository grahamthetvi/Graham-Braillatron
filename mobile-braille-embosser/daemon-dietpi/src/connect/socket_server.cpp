#include "socket_server.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::connect {

SocketServer::SocketServer(std::string path)
    : path_(std::move(path))
{
}

SocketServer::~SocketServer()
{
    close();
}

bool SocketServer::listen()
{
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path_.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
    unlink(path_.c_str());

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[connectd] bind failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (::listen(listen_fd_, 1) < 0) {
        return false;
    }
    return true;
}

void SocketServer::poll_once(const std::function<std::string(const std::string &)> &handler)
{
    if (listen_fd_ < 0) {
        return;
    }

    if (client_fd_ < 0) {
        client_fd_ = accept(listen_fd_, nullptr, nullptr);
        if (client_fd_ < 0) {
            return;
        }
    }

    char buffer[8192];
    const ssize_t n = recv(client_fd_, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
        return;
    }
    if (n == 0) {
        ::close(client_fd_);
        client_fd_ = -1;
        return;
    }

    buffer[n] = '\0';
    std::string request(buffer);
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }
    if (request.empty()) {
        return;
    }

    const std::string response = handler(request);
    std::string out = response;
    if (out.empty() || out.back() != '\n') {
        out += '\n';
    }
    send(client_fd_, out.c_str(), out.size(), 0);
    ::close(client_fd_);
    client_fd_ = -1;
}

void SocketServer::close()
{
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty()) {
        unlink(path_.c_str());
    }
}

} // namespace braillatron::connect
