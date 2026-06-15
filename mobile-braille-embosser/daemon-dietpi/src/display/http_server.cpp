#include "http_server.h"

#include "frame_protocol.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace braillatron::display {

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string read_file_or_empty(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

const char *kEmbeddedIndexHtml = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Braillatron Remote Display</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #111; color: #eee; margin: 2rem; }
    #screen { image-rendering: pixelated; border: 2px solid #444; background: #000; }
    form, .status { max-width: 30rem; }
    input, button { font-size: 1rem; padding: 0.5rem; margin-top: 0.5rem; }
    #viewer { display: none; }
  </style>
</head>
<body>
  <div id="login">
    <h1>Braillatron Remote Display</h1>
    <p>Enter the 6-digit pairing code shown on the device.</p>
    <form id="pair-form">
      <input id="code" maxlength="6" pattern="[0-9]{6}" required placeholder="123456">
      <button type="submit">Pair</button>
    </form>
    <p id="error" style="color:#f88"></p>
  </div>
  <div id="viewer">
    <p class="status" id="status">Connected</p>
    <canvas id="screen" width="240" height="240"></canvas>
    <button id="logout">Logout</button>
  </div>
  <script src="/viewer.js"></script>
</body>
</html>)";

const char *kEmbeddedViewerJs = R"(async function pair(code) {
  const res = await fetch('/api/pair', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code })
  });
  return res.ok;
}

function showViewer() {
  document.getElementById('login').style.display = 'none';
  document.getElementById('viewer').style.display = 'block';
  startStream();
}

document.getElementById('pair-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const code = document.getElementById('code').value.trim();
  const ok = await pair(code);
  if (!ok) {
    document.getElementById('error').textContent = 'Invalid or expired pairing code.';
    return;
  }
  showViewer();
});

document.getElementById('logout').addEventListener('click', async () => {
  await fetch('/api/logout', { method: 'POST' });
  location.reload();
});

async function checkSession() {
  const res = await fetch('/api/status');
  if (res.ok) {
    showViewer();
  }
}

function startStream() {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d');
  const ws = new WebSocket((location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws/frame');
  ws.binaryType = 'arraybuffer';
  ws.onmessage = (event) => {
    const view = new DataView(event.data);
    const width = view.getUint16(8, true);
    const height = view.getUint16(10, true);
    const offset = 16;
    const pixels = new Uint16Array(event.data, offset);
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const image = ctx.createImageData(width, height);
    for (let i = 0; i < pixels.length; ++i) {
      const rgb565 = pixels[i];
      const r = ((rgb565 >> 11) & 0x1f) * 255 / 31;
      const g = ((rgb565 >> 5) & 0x3f) * 255 / 63;
      const b = (rgb565 & 0x1f) * 255 / 31;
      const j = i * 4;
      image.data[j] = r;
      image.data[j + 1] = g;
      image.data[j + 2] = b;
      image.data[j + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
    document.getElementById('status').textContent = 'Live (' + width + 'x' + height + ')';
  };
  ws.onclose = () => {
    document.getElementById('status').textContent = 'Disconnected — reconnecting…';
    setTimeout(startStream, 2000);
  };
}

checkSession();
)";

std::string base64_encode(const uint8_t *data, size_t len)
{
    static const char table[] =
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

struct Sha256 {
    void update(const std::string &text)
    {
        update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    }
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
    std::array<uint8_t, 32> finalize()
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
        std::array<uint8_t, 32> digest {};
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
    static uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static uint32_t gam0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static uint32_t gam1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
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
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
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

std::string websocket_accept_key(const std::string &client_key)
{
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha256 sha;
    sha.update(client_key + magic);
    const auto digest = sha.finalize();
    return base64_encode(digest.data(), 20);
}

} // namespace

HttpServer::HttpServer(RemoteDisplayConfig config, PairingAuth &auth)
    : config_(std::move(config))
    , auth_(auth)
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    if (running_) {
        return true;
    }

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    const int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.listen_port);
    const std::string bind_addr = config_.allow_lan ? "0.0.0.0" : config_.listen_address;
    if (inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[displayd] bind failed on " << bind_addr << ":" << config_.listen_port
                  << "\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 8) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    std::thread([this]() { accept_loop(); }).detach();
    std::cerr << "[displayd] HTTP listening on " << bind_addr << ":" << config_.listen_port
              << "\n";
    return true;
}

void HttpServer::stop()
{
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : websocket_clients_) {
        close(fd);
    }
    websocket_clients_.clear();
}

void HttpServer::accept_loop()
{
    while (running_) {
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        std::thread([this, client_fd]() {
            handle_client(client_fd);
            close(client_fd);
        }).detach();
    }
}

void HttpServer::handle_client(int client_fd)
{
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            return;
        }
        buffer[n] = '\0';
        request.append(buffer, static_cast<size_t>(n));
        if (request.size() > 65536) {
            return;
        }
    }

    if (request.rfind("GET /ws/frame", 0) == 0) {
        if (handle_websocket(client_fd, request)) {
            return;
        }
    }
    handle_http_request(client_fd, request);
}

std::string HttpServer::session_from_cookie(const std::string &cookie_header) const
{
    const std::string key = "braillatron_session=";
    const size_t pos = cookie_header.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + key.size();
    const size_t end = cookie_header.find(';', start);
    if (end == std::string::npos) {
        return trim(cookie_header.substr(start));
    }
    return trim(cookie_header.substr(start, end - start));
}

bool HttpServer::handle_http_request(int client_fd, const std::string &request)
{
    const size_t line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, line_end);
    std::istringstream line_stream(request_line);
    std::string method;
    std::string path;
    std::string version;
    line_stream >> method >> path >> version;

    std::string cookie_header;
    const size_t cookie_pos = request.find("Cookie:");
    if (cookie_pos != std::string::npos) {
        const size_t line_end_cookie = request.find("\r\n", cookie_pos);
        cookie_header = request.substr(cookie_pos + 8, line_end_cookie - cookie_pos - 8);
    }
    const std::string session = session_from_cookie(cookie_header);

    auto send_response = [&](int code, const std::string &status, const std::string &content_type,
                             const std::string &body,
                             const std::string &extra_headers = "") {
        std::ostringstream response;
        response << "HTTP/1.1 " << code << ' ' << status << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Content-Length: " << body.size() << "\r\n";
        response << "Connection: close\r\n";
        if (!extra_headers.empty()) {
            response << extra_headers;
        }
        response << "\r\n" << body;
        const std::string payload = response.str();
        send(client_fd, payload.data(), payload.size(), MSG_NOSIGNAL);
    };

    if (method == "GET" && path == "/") {
        std::string body = read_file_or_empty(config_.static_dir + "/index.html");
        if (body.empty()) {
            body = kEmbeddedIndexHtml;
        }
        send_response(200, "OK", "text/html; charset=utf-8", body);
        return true;
    }

    if (method == "GET" && path == "/viewer.js") {
        std::string body = read_file_or_empty(config_.static_dir + "/viewer.js");
        if (body.empty()) {
            body = kEmbeddedViewerJs;
        }
        send_response(200, "OK", "application/javascript; charset=utf-8", body);
        return true;
    }

    if (method == "GET" && path == "/api/status") {
        if (!auth_.validate_session(session)) {
            send_response(401, "Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
            return true;
        }
        std::ostringstream body;
        body << "{\"connected\":true,\"width\":" << latest_width_ << ",\"height\":" << latest_height_
             << ",\"clients\":" << client_count() << "}";
        send_response(200, "OK", "application/json", body.str());
        return true;
    }

    if (method == "POST" && path == "/api/pair") {
        if (auth_.is_pairing_locked()) {
            send_response(429, "Too Many Requests", "application/json",
                          "{\"error\":\"pairing_locked\"}");
            return true;
        }
        const size_t body_pos = request.find("\r\n\r\n");
        const std::string body = body_pos == std::string::npos ? "" : request.substr(body_pos + 4);
        const size_t code_pos = body.find("\"code\"");
        std::string code;
        if (code_pos != std::string::npos) {
            const size_t quote = body.find('"', code_pos + 6);
            const size_t quote2 = body.find('"', quote + 1);
            if (quote != std::string::npos && quote2 != std::string::npos) {
                code = body.substr(quote + 1, quote2 - quote - 1);
            }
        }
        if (!auth_.verify_pairing_code(code)) {
            auth_.record_pairing_failure();
            send_response(401, "Unauthorized", "application/json", "{\"error\":\"invalid_code\"}");
            return true;
        }
        auth_.clear_active_pairing();
        const std::string token = auth_.create_session();
        send_response(200, "OK", "application/json", "{\"ok\":true}",
                      "Set-Cookie: braillatron_session=" + token +
                          "; HttpOnly; Path=/; SameSite=Strict\r\n");
        return true;
    }

    if (method == "POST" && path == "/api/logout") {
        auth_.revoke_session(session);
        send_response(200, "OK", "application/json", "{\"ok\":true}",
                      "Set-Cookie: braillatron_session=; HttpOnly; Path=/; Max-Age=0\r\n");
        return true;
    }

    send_response(404, "Not Found", "text/plain", "Not found");
    return true;
}

bool HttpServer::handle_websocket(int client_fd, const std::string &request)
{
    std::string cookie_header;
    const size_t cookie_pos = request.find("Cookie:");
    if (cookie_pos != std::string::npos) {
        const size_t line_end_cookie = request.find("\r\n", cookie_pos);
        cookie_header = request.substr(cookie_pos + 8, line_end_cookie - cookie_pos - 8);
    }
    if (!auth_.validate_session(session_from_cookie(cookie_header))) {
        const char *response =
            "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(client_fd, response, std::strlen(response), MSG_NOSIGNAL);
        return false;
    }

    std::string client_key;
    const size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos != std::string::npos) {
        const size_t line_end = request.find("\r\n", key_pos);
        client_key = trim(request.substr(key_pos + 18, line_end - key_pos - 18));
    }
    if (client_key.empty()) {
        return false;
    }

    const std::string accept = websocket_accept_key(client_key);
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
    const std::string payload = response.str();
    send(client_fd, payload.data(), payload.size(), MSG_NOSIGNAL);

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        websocket_clients_.push_back(client_fd);
        if (!latest_pixels_.empty()) {
            std::vector<uint8_t> packet(sizeof(FrameHeader) + latest_pixels_.size() * 2);
            FrameHeader header {};
            header.frame_id = latest_frame_id_;
            header.width = latest_width_;
            header.height = latest_height_;
            header.payload_bytes = static_cast<uint32_t>(latest_pixels_.size() * 2);
            std::memcpy(packet.data(), &header, sizeof(header));
            std::memcpy(packet.data() + sizeof(header), latest_pixels_.data(),
                        latest_pixels_.size() * 2);
            send_websocket_binary(client_fd, packet.data(), packet.size());
        }
    }

    char buffer[256];
    while (running_) {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        websocket_clients_.erase(
            std::remove(websocket_clients_.begin(), websocket_clients_.end(), client_fd),
            websocket_clients_.end());
    }
    return true;
}

void HttpServer::send_websocket_binary(int client_fd, const uint8_t *data, size_t len)
{
    std::vector<uint8_t> frame;
    frame.push_back(0x82);
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        return;
    }
    frame.insert(frame.end(), data, data + len);
    send(client_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
}

void HttpServer::publish_frame(const std::vector<uint16_t> &pixels, uint16_t width,
                               uint16_t height)
{
    FrameHeader header {};
    header.frame_id = ++latest_frame_id_;
    header.width = width;
    header.height = height;
    header.payload_bytes = static_cast<uint32_t>(pixels.size() * sizeof(uint16_t));

    std::vector<uint8_t> packet(sizeof(FrameHeader) + pixels.size() * sizeof(uint16_t));
    std::memcpy(packet.data(), &header, sizeof(header));
    std::memcpy(packet.data() + sizeof(header), pixels.data(), pixels.size() * sizeof(uint16_t));

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        latest_pixels_ = pixels;
        latest_width_ = width;
        latest_height_ = height;
        for (int fd : websocket_clients_) {
            send_websocket_binary(fd, packet.data(), packet.size());
        }
    }
}

size_t HttpServer::client_count() const
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return websocket_clients_.size();
}

std::string HttpServer::resolve_static(const std::string &path) const
{
    return config_.static_dir + path;
}

} // namespace braillatron::display
