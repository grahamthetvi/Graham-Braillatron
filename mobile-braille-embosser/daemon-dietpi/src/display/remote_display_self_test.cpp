#include "frame_protocol.h"
#include "pairing_auth_self_test.h"
#include "remote_display_config.h"

#include <cstring>
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

bool expect_true(bool value, const char *label)
{
    if (!value) {
        std::cerr << label << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    braillatron::display::FrameHeader header;
    header.frame_id = 7;
    header.width = 240;
    header.height = 240;
    std::vector<uint16_t> pixels(240 * 240, 0xFFFF);
    header.crc32 = braillatron::display::crc32_rgb565(pixels.data(), pixels.size());

    const auto packet =
        braillatron::display::encode_frame_packet(header, pixels.data(), pixels.size());
    braillatron::display::FrameHeader decoded;
    std::vector<uint16_t> decoded_pixels;
    if (!expect_true(braillatron::display::decode_frame_packet(packet.data(), packet.size(),
                                                               decoded, decoded_pixels),
                     "decode frame packet")) {
        return 1;
    }
    if (!expect_true(decoded.frame_id == 7, "frame id mismatch")) {
        return 1;
    }
    if (!expect_true(decoded_pixels.size() == pixels.size(), "pixel count mismatch")) {
        return 1;
    }

    const std::string config_path = "/tmp/braillatron-remote-display-test.conf";
    braillatron::display::RemoteDisplayConfig config;
    config.enabled = true;
    config.allow_lan = false;
    config.listen_port = 8080;
    if (!expect_true(braillatron::display::save_remote_display_config(config_path, config),
                     "save remote display config")) {
        return 1;
    }
    const auto loaded = braillatron::display::load_remote_display_config(config_path);
    if (!expect_true(loaded.enabled && !loaded.allow_lan && loaded.listen_port == 8080,
                     "load remote display config")) {
        return 1;
    }

    const char *socket_path = "/tmp/braillatron-display-test.sock";
    unlink(socket_path);
    const int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (!expect_true(listen_fd >= 0, "create listener")) {
        return 1;
    }
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    listen(listen_fd, 1);

    bool received = false;
    std::thread server([&]() {
        const int client = accept(listen_fd, nullptr, nullptr);
        if (client < 0) {
            return;
        }
        std::vector<uint8_t> buffer(packet.size());
        size_t offset = 0;
        while (offset < buffer.size()) {
            const ssize_t n = recv(client, buffer.data() + offset, buffer.size() - offset, 0);
            if (n <= 0) {
                break;
            }
            offset += static_cast<size_t>(n);
        }
        close(client);
        received = offset == buffer.size();
    });

    const int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un client_addr {};
    client_addr.sun_family = AF_UNIX;
    std::strncpy(client_addr.sun_path, socket_path, sizeof(client_addr.sun_path) - 1);
    connect(client_fd, reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr));
    send(client_fd, packet.data(), packet.size(), 0);
    close(client_fd);

    server.join();
    close(listen_fd);
    unlink(socket_path);
    if (!expect_true(received, "frame socket write")) {
        return 1;
    }

    if (run_pairing_auth_self_test() != 0) {
        return 1;
    }

    std::cout << "remote display self-test passed\n";
    return 0;
}
