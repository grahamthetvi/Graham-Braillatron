#include "http_server.h"
#include "virtual_keyboard.h"

#include "../connect/json_utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace braillatron::display {

namespace {

constexpr const char *kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string base64_encode(const uint8_t *data, size_t len)
{
    static const char *table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t block =
            (static_cast<uint32_t>(data[i]) << 16) |
            ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
        out.push_back(table[(block >> 18) & 0x3F]);
        out.push_back(table[(block >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? table[(block >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? table[block & 0x3F] : '=');
    }
    return out;
}

void sha1(const std::string &input, uint8_t out[20])
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(input.begin(), input.end());
    const uint64_t bit_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(msg[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) << 1) |
                   ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) >> 31);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t temp = (((a << 5) | (a >> 27)) + f + e + k + w[i]);
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const uint32_t state[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
    }
}

std::string header_value(const std::string &request, const std::string &name)
{
    const std::string needle = name + ":";
    const size_t pos = request.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < request.size() && request[start] == ' ') {
        ++start;
    }
    size_t end = request.find("\r\n", start);
    if (end == std::string::npos) {
        end = request.size();
    }
    return request.substr(start, end - start);
}

std::string request_path(const std::string &request)
{
    const size_t start = request.find(' ');
    if (start == std::string::npos) {
        return "/";
    }
    const size_t end = request.find(' ', start + 1);
    if (end == std::string::npos) {
        return "/";
    }
    return request.substr(start + 1, end - start - 1);
}

std::string request_method(const std::string &request)
{
    const size_t end = request.find(' ');
    if (end == std::string::npos) {
        return "GET";
    }
    return request.substr(0, end);
}

std::string request_body(const std::string &request)
{
    const size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return {};
    }
    return request.substr(pos + 4);
}

} // namespace

HttpServer::HttpServer(std::string bind_address, uint16_t port, std::string static_root,
                         PairingAuth *auth, VirtualKeyboard *kb)
    : bind_address_(std::move(bind_address))
    , port_(port)
    , static_root_(std::move(static_root))
    , auth_(auth)
    , kb_(kb)
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[displayd] invalid bind address " << bind_address_ << "\n";
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[displayd] bind failed on " << bind_address_ << ":" << port_ << ": "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (::listen(listen_fd_, 8) < 0) {
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread([this]() { accept_loop(); });
    return true;
}

void HttpServer::stop()
{
    running_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const Client &client : clients_) {
        if (client.fd >= 0) {
            ::close(client.fd);
        }
    }
    clients_.clear();
    connected_clients_ = 0;
}

void HttpServer::accept_loop()
{
    while (running_) {
        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (running_) {
                continue;
            }
            break;
        }
        handle_client(fd);
    }
}

std::string HttpServer::session_from_cookie(const std::string &request) const
{
    const std::string cookie = header_value(request, "Cookie");
    const std::string needle = "braillatron_session=";
    const size_t pos = cookie.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    size_t end = cookie.find(';', start);
    if (end == std::string::npos) {
        end = cookie.size();
    }
    return cookie.substr(start, end - start);
}

void HttpServer::send_http_response(int fd, int status, const std::string &content_type,
                                    const std::string &body, const std::string &extra_headers)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << (status == 200 ? " OK\r\n" : " \r\n");
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    if (!extra_headers.empty()) {
        response << extra_headers;
    }
    response << "\r\n" << body;
    const std::string payload = response.str();
    send(fd, payload.c_str(), payload.size(), 0);
}

std::string HttpServer::read_static_file(const std::string &path) const
{
    std::ifstream in(static_root_ + path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool HttpServer::upgrade_websocket(int fd, const std::string &request,
                                   const std::string &session_token)
{
    if (auth_ == nullptr || !auth_->validate_session(session_token)) {
        send_http_response(fd, 401, "text/plain", "Unauthorized");
        return false;
    }

    const std::string key = header_value(request, "Sec-WebSocket-Key");
    if (key.empty()) {
        send_http_response(fd, 400, "text/plain", "Missing websocket key");
        return false;
    }

    uint8_t digest[20];
    sha1(key + kWsGuid, digest);
    const std::string accept = base64_encode(digest, 20);

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
    const std::string payload = response.str();
    send(fd, payload.c_str(), payload.size(), 0);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.push_back(Client {fd, true});
    connected_clients_ = static_cast<uint32_t>(clients_.size());

    if (has_frame_) {
        const auto packet = encode_frame_packet(latest_header_, latest_pixels_.data(), latest_pixels_.size());
        std::vector<uint8_t> frame;
        frame.reserve(10 + packet.size());
        frame.push_back(0x82);
        if (packet.size() <= 125) {
            frame.push_back(static_cast<uint8_t>(packet.size()));
        } else if (packet.size() <= 65535) {
            frame.push_back(126);
            frame.push_back(static_cast<uint8_t>((packet.size() >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(packet.size() & 0xFF));
        } else {
            frame.push_back(127);
            const uint64_t len = packet.size();
            for (int i = 7; i >= 0; --i) {
                frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
            }
        }
        frame.insert(frame.end(), packet.begin(), packet.end());
        std::cerr << "[displayd] sending cached frame to new client (" << frame.size() << " bytes)\n";
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    } else {
        std::cerr << "[displayd] no cached frame for new client\n";
        std::ofstream need_frame_flag("/run/braillatron/need-frame", std::ios::trunc);
        FILE *fp = popen("pidof -s braillatron-ui 2>/dev/null", "r");
        if (fp != nullptr) {
            char buffer[32] = {};
            if (std::fgets(buffer, sizeof(buffer), fp) != nullptr) {
                const pid_t ui_pid = static_cast<pid_t>(std::strtol(buffer, nullptr, 10));
                if (ui_pid > 0) {
                    kill(ui_pid, SIGUSR1);
                }
            }
            pclose(fp);
        }
    }

    return true;
}

bool HttpServer::handle_http_request(int fd, const std::string &request)
{
    const std::string method = request_method(request);
    const std::string path = request_path(request);

    if (method == "GET" && path == "/") {
        const std::string body = read_static_file("/index.html");
        send_http_response(fd, body.empty() ? 404 : 200, "text/html",
                           body.empty() ? "Not found" : body);
        return false;
    }

    if (method == "GET" && path == "/viewer.js") {
        const std::string body = read_static_file("/viewer.js");
        send_http_response(fd, body.empty() ? 404 : 200, "application/javascript",
                           body.empty() ? "Not found" : body);
        return false;
    }

    if (method == "GET" && path == "/api/status") {
        const std::string session = session_from_cookie(request);
        if (auth_ == nullptr || !auth_->validate_session(session)) {
            send_http_response(fd, 401, "application/json", "{\"ok\":false}");
            return false;
        }
        std::ostringstream body;
        body << "{\"ok\":true,\"clients\":" << connected_clients_.load() << "}";
        send_http_response(fd, 200, "application/json", body.str());
        return false;
    }

    if (method == "POST" && path == "/api/pair") {
        if (auth_ == nullptr) {
            send_http_response(fd, 503, "application/json", "{\"ok\":false}");
            return false;
        }
        const std::string body = request_body(request);
        const std::string code = braillatron::connect::json_get_string(body, "code");
        const auto token = auth_->verify_pairing(code);
        if (!token.has_value()) {
            const std::string error = auth_->last_verify_error();
            const std::string body = error.empty()
                                         ? "{\"ok\":false,\"error\":\"invalid_code\"}"
                                         : "{\"ok\":false,\"error\":\"" +
                                               braillatron::connect::json_escape(error) + "\"}";
            send_http_response(fd, 403, "application/json", body);
            return false;
        }
        const std::string cookie =
            "Set-Cookie: braillatron_session=" + *token +
            "; HttpOnly; Path=/; SameSite=Strict\r\n";
        send_http_response(fd, 200, "application/json", "{\"ok\":true}", cookie);
        return false;
    }

    if (method == "POST" && path == "/api/logout") {
        const std::string session = session_from_cookie(request);
        if (auth_ != nullptr && !session.empty()) {
            auth_->revoke_session(session);
        }
        send_http_response(fd, 200, "application/json", "{\"ok\":true}",
                           "Set-Cookie: braillatron_session=; HttpOnly; Path=/; Max-Age=0\r\n");
        return false;
    }

    if (method == "GET" && path == "/ws/frame") {
        return upgrade_websocket(fd, request, session_from_cookie(request));
    }

    send_http_response(fd, 404, "text/plain", "Not found");
    return false;
}

void HttpServer::handle_client(int fd)
{
    char buffer[16384];
    const ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        ::close(fd);
        return;
    }
    buffer[n] = '\0';
    const std::string request(buffer);
    if (handle_http_request(fd, request)) {
        return;
    }
    ::close(fd);
}

void HttpServer::remove_client(int fd)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [fd](const Client &client) { return client.fd == fd; }),
                   clients_.end());
    connected_clients_ = static_cast<uint32_t>(clients_.size());
}

void HttpServer::broadcast_frame(const FrameHeader &header, const std::vector<uint16_t> &pixels)
{
    const auto packet = encode_frame_packet(header, pixels.data(), pixels.size());
    std::vector<uint8_t> frame;
    frame.reserve(10 + packet.size());
    frame.push_back(0x82);
    if (packet.size() <= 125) {
        frame.push_back(static_cast<uint8_t>(packet.size()));
    } else if (packet.size() <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((packet.size() >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(packet.size() & 0xFF));
    } else {
        frame.push_back(127);
        const uint64_t len = packet.size();
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), packet.begin(), packet.end());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    latest_header_ = header;
    latest_pixels_ = pixels;
    has_frame_ = true;
    std::cerr << "[displayd] cached frame " << header.width << "x" << header.height
              << " (" << packet.size() << " bytes)\n";

    std::vector<int> dead;
    for (const Client &client : clients_) {
        if (!client.websocket || client.fd < 0) {
            continue;
        }
        if (send(client.fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0) {
            dead.push_back(client.fd);
            ::close(client.fd);
        }
    }
    if (!dead.empty()) {
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                      [&dead](const Client &client) {
                                          return std::find(dead.begin(), dead.end(), client.fd) !=
                                                 dead.end();
                                      }),
                        clients_.end());
        connected_clients_ = static_cast<uint32_t>(clients_.size());
    }
}

void HttpServer::broadcast_speak(const std::string &text)
{
    std::string payload = "{\"type\":\"speak\",\"text\":\"" + braillatron::connect::json_escape(text) + "\"}";
    std::vector<uint8_t> frame;
    frame.reserve(10 + payload.size());
    frame.push_back(0x81);
    if (payload.size() <= 125) {
        frame.push_back(static_cast<uint8_t>(payload.size()));
    } else if (payload.size() <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    } else {
        frame.push_back(127);
        const uint64_t len = payload.size();
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), payload.begin(), payload.end());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<int> dead;
    for (const Client &client : clients_) {
        if (!client.websocket || client.fd < 0) {
            continue;
        }
        if (send(client.fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0) {
            dead.push_back(client.fd);
            ::close(client.fd);
        }
    }
    if (!dead.empty()) {
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                      [&dead](const Client &client) {
                                          return std::find(dead.begin(), dead.end(), client.fd) !=
                                                 dead.end();
                                      }),
                        clients_.end());
        connected_clients_ = static_cast<uint32_t>(clients_.size());
    }
}

namespace {

bool recv_all(int fd, uint8_t *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, buf + total, len - total, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            return false;
        }
        total += n;
    }
    return true;
}

} // namespace

void HttpServer::poll_clients()
{
    std::vector<Client> active_clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        active_clients = clients_;
    }

    if (active_clients.empty()) {
        return;
    }

    std::vector<pollfd> pfds;
    pfds.reserve(active_clients.size());
    for (const auto &client : active_clients) {
        if (client.websocket && client.fd >= 0) {
            pollfd pfd {};
            pfd.fd = client.fd;
            pfd.events = POLLIN;
            pfds.push_back(pfd);
        }
    }

    if (pfds.empty()) {
        return;
    }

    int ret = poll(pfds.data(), pfds.size(), 0);
    if (ret <= 0) {
        return;
    }

    std::vector<int> dead_fds;

    for (const auto &pfd : pfds) {
        if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) {
            if (pfd.revents & (POLLERR | POLLHUP)) {
                dead_fds.push_back(pfd.fd);
                continue;
            }

            uint8_t header[2];
            if (!recv_all(pfd.fd, header, 2)) {
                dead_fds.push_back(pfd.fd);
                continue;
            }

            uint8_t opcode = header[0] & 0x0F;
            bool masked = (header[1] & 0x80) != 0;
            uint64_t payload_len = header[1] & 0x7F;

            if (payload_len == 126) {
                uint8_t ext_len[2];
                if (!recv_all(pfd.fd, ext_len, 2)) {
                    dead_fds.push_back(pfd.fd);
                    continue;
                }
                payload_len = (static_cast<uint64_t>(ext_len[0]) << 8) | ext_len[1];
            } else if (payload_len == 127) {
                uint8_t ext_len[8];
                if (!recv_all(pfd.fd, ext_len, 8)) {
                    dead_fds.push_back(pfd.fd);
                    continue;
                }
                payload_len = 0;
                for (int i = 0; i < 8; ++i) {
                    payload_len = (payload_len << 8) | ext_len[i];
                }
            }

            uint8_t mask_key[4] = {0};
            if (masked) {
                if (!recv_all(pfd.fd, mask_key, 4)) {
                    dead_fds.push_back(pfd.fd);
                    continue;
                }
            }

            if (opcode == 0x08) { // Connection close
                dead_fds.push_back(pfd.fd);
                continue;
            }

            std::vector<uint8_t> payload(payload_len);
            if (payload_len > 0) {
                if (!recv_all(pfd.fd, payload.data(), payload_len)) {
                    dead_fds.push_back(pfd.fd);
                    continue;
                }

                if (masked) {
                    for (uint64_t i = 0; i < payload_len; ++i) {
                        payload[i] ^= mask_key[i % 4];
                    }
                }
            }

            if (opcode == 0x01) { // Text frame
                std::string msg(payload.begin(), payload.end());
                handle_websocket_message(pfd.fd, msg);
            }
        }
    }

    if (!dead_fds.empty()) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : dead_fds) {
            ::close(fd);
            clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                          [fd](const Client &client) { return client.fd == fd; }),
                           clients_.end());
        }
        connected_clients_ = static_cast<uint32_t>(clients_.size());
    }
}

void HttpServer::handle_websocket_message(int /*fd*/, const std::string &msg)
{
    const std::string type = braillatron::connect::json_get_string(msg, "type");
    if (type == "keydown" || type == "keyup") {
        const std::string key_str = braillatron::connect::json_get_string(msg, "key");
        if (!key_str.empty() && kb_ != nullptr) {
            try {
                int keycode = std::stoi(key_str);
                kb_->send_key(keycode, type == "keydown");
            } catch (...) {
                // Ignore parsing/stoi exceptions
            }
        }
    }
}

} // namespace braillatron::display
