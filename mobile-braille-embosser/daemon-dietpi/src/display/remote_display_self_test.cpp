#include "frame_protocol.h"
#include "pairing_auth.h"
#include "remote_display_config.h"

#include <chrono>
#include <iostream>
#include <thread>

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
    braillatron::display::FrameHeader header {};
    header.magic = braillatron::display::kFrameMagic;
    header.width = 240;
    header.height = 240;
    header.payload_bytes = 240U * 240U * 2U;
    if (!expect_true(braillatron::display::validate_frame_header(header), "frame header valid")) {
        return 1;
    }
    header.payload_bytes = 10;
    if (!expect_true(!braillatron::display::validate_frame_header(header),
                     "frame header rejects bad payload")) {
        return 1;
    }

    braillatron::display::PairingAuth auth(30);
    const std::string code = "123456";
    const std::string hash = auth.hash_code(code);
    auth.set_active_pairing_hash(hash, std::chrono::steady_clock::now() + std::chrono::minutes(5));
    if (!expect_true(auth.verify_pairing_code(code), "pairing code verifies")) {
        return 1;
    }
    if (!expect_true(!auth.verify_pairing_code("000000"), "wrong pairing code rejected")) {
        return 1;
    }

    for (int i = 0; i < 5; ++i) {
        auth.record_pairing_failure();
    }
    if (!expect_true(auth.is_pairing_locked(), "pairing lock after failures")) {
        return 1;
    }

    const std::string token = auth.create_session();
    if (!expect_true(auth.validate_session(token), "session token validates")) {
        return 1;
    }
    if (!expect_true(!auth.validate_session("bad-token"), "invalid session rejected")) {
        return 1;
    }

    const std::string path = "/tmp/braillatron-remote-display-test.conf";
    braillatron::display::RemoteDisplayConfig cfg;
    cfg.enabled = true;
    cfg.listen_port = 9090;
    cfg.allow_lan = true;
    braillatron::display::save_remote_display_config(path, cfg);
    const auto loaded = braillatron::display::load_remote_display_config(path);
    if (!expect_true(loaded.enabled && loaded.listen_port == 9090 && loaded.allow_lan,
                     "remote display config round trip")) {
        return 1;
    }

    std::cout << "remote-display self-test passed\n";
    return 0;
}
