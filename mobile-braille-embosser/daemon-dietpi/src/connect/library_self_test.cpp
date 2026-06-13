#include "library_backend.h"

#include "connect_config.h"
#include "json_utils.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string temp_dir()
{
    const char *env = std::getenv("TMPDIR");
    const std::string base =
        (env != nullptr && env[0] != '\0') ? env : "braillatron-self-test-tmp";
    return base + "/library-backend-" + std::to_string(::getpid());
}

bool test_list_local_empty()
{
    braillatron::connect::LibraryConfig config;
    config.catalog_path = temp_dir() + "/missing-catalog.json";
    braillatron::connect::LibraryBackend backend(config);
    const std::string response = backend.list_local();
    expect_true(braillatron::connect::json_get_bool(response, "ok", false), "list_local ok");
    expect_true(response.find("\"books\":[]") != std::string::npos, "empty books array");
    return true;
}

bool test_status_disabled()
{
    braillatron::connect::LibraryConfig config;
    config.enabled = false;
    braillatron::connect::LibraryBackend backend(config);
    const std::string response = backend.search("test");
    expect_true(!braillatron::connect::json_get_bool(response, "ok", true), "disabled search fails");
    return true;
}

} // namespace

int main()
{
    test_list_local_empty();
    test_status_disabled();

    if (failures != 0) {
        std::cerr << failures << " library backend self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "library backend self-test passed\n";
    return EXIT_SUCCESS;
}
