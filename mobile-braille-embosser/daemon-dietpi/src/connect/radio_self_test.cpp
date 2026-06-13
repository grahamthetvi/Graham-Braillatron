#include "radio_backend.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    return "/tmp/braillatron-radio-self-test-" + std::to_string(::getpid());
}

bool write_file(const std::string &path, const std::string &contents)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << contents;
    return true;
}

void test_parse_stations_json()
{
    const std::string json = R"({
  "stations": [
    {"id": "s1", "name": "Station One", "url": "http://example.com/stream", "country": "US", "tags": "news"},
    {"id": "s2", "name": "Station Two", "url": "http://example.com/stream2", "country": "UK", "tags": "music"}
  ]
})";

    const std::vector<braillatron::connect::RadioStation> stations =
        braillatron::connect::RadioBackend::parse_stations_json(json);
    expect_true(stations.size() == 2, "station count");
    expect_true(stations[0].name == "Station One", "first station name");
    expect_true(stations[1].country == "UK", "second station country");
}

void test_parse_icy_title()
{
    const std::string headers =
        "HTTP/1.0 200 OK\r\n"
        "icy-name:Classic FM\r\n"
        "Content-Type: audio/mpeg\r\n";
    expect_true(braillatron::connect::RadioBackend::parse_icy_title(headers) == "Classic FM",
                "icy-name header");
}

void test_favorites_round_trip()
{
    const std::string dir = temp_dir();
    const std::string stations_path = dir + "/stations.json";
    const std::string favorites_path = dir + "/favorites.json";

    expect_true(write_file(stations_path, R"({
  "stations": [
    {"id": "fav-test", "name": "Test FM", "url": "http://example.com/live", "country": "US", "tags": "test"}
  ]
})"),
                "write stations");

    braillatron::connect::RadioConfig config;
    config.stations_path = stations_path;
    config.favorites_path = favorites_path;
    config.enabled = true;

    braillatron::connect::RadioBackend backend(config, nullptr, nullptr);
    const std::string add = backend.favorites_add("fav-test");
    expect_true(add.find("\"ok\":true") != std::string::npos, "add favorite");

    const std::string list = backend.favorites_list();
    expect_true(list.find("Test FM") != std::string::npos, "favorite listed");

    const std::string remove = backend.favorites_remove("fav-test");
    expect_true(remove.find("\"ok\":true") != std::string::npos, "remove favorite");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main()
{
    test_parse_stations_json();
    test_parse_icy_title();
    test_favorites_round_trip();

    if (failures > 0) {
        std::cerr << failures << " radio self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "radio self-test passed\n";
    return EXIT_SUCCESS;
}
