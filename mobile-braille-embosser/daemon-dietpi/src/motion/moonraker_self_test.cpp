#include "klipper_config.h"
#include "moonraker_client.h"

#include <iostream>
#include <string>

namespace {

void expect_true(bool condition, const char *label)
{
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        std::exit(1);
    }
    std::cerr << "ok: " << label << "\n";
}

} // namespace

int main()
{
    braillatron::motion::KlipperConfig config;
    config.enabled = false;
    config.moonraker_url = "http://127.0.0.1:7125";

    braillatron::motion::MoonrakerClient client(config);
    expect_true(!client.ping(), "disabled client does not ping");

    expect_true(!client.emboss_dot(0, 2.0, 40.0), "dot index 0 rejected");
    expect_true(!client.emboss_dot(7, 2.0, 40.0), "dot index 7 rejected");

    braillatron::motion::KlipperConfig loaded =
        braillatron::motion::load_klipper_config("config/klipper.conf");
    expect_true(loaded.enabled, "klipper.conf enabled");
    expect_true(loaded.moonraker_url.find("7125") != std::string::npos, "moonraker port");
    expect_true(loaded.emboss_stroke_mm > 0.0, "emboss stroke configured");
    expect_true(loaded.emboss_speed_mm_s > 0.0, "emboss speed configured");

    std::cerr << "braillatron-moonraker-test passed\n";
    return 0;
}
