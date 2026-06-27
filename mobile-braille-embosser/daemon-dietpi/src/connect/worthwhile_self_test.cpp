#include "worthwhile_backend.h"

#include "connect_config.h"
#include "json_utils.h"

#include <iostream>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    braillatron::connect::WorthwhileConfig config;
    config.enabled = false;
    braillatron::connect::WorthwhileBackend backend(config);
    const std::string search = backend.search("test", "movies");
    expect_true(!braillatron::connect::json_get_bool(search, "ok", true), "disabled search fails");
    const std::string status = backend.status();
    expect_true(braillatron::connect::json_get_bool(status, "ok", false), "status ok");

    if (failures != 0) {
        std::cerr << failures << " worthwhile backend self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "worthwhile backend self-test passed\n";
    return EXIT_SUCCESS;
}
