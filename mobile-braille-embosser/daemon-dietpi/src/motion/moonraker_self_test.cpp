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

    expect_true(config.emboss_stepper_name(1) == "emboss_1", "emboss stepper 1 default");
    expect_true(config.emboss_stepper_name(7).empty(), "emboss stepper 7 empty");

    braillatron::motion::KlipperConfig loaded =
        braillatron::motion::load_klipper_config("config/klipper.conf");
    expect_true(loaded.enabled, "klipper.conf enabled");
    expect_true(loaded.moonraker_url.find("7125") != std::string::npos, "moonraker port");

    std::cerr << "braillatron-moonraker-test passed\n";
    return 0;
}
