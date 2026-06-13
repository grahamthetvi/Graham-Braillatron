#include "music_backend.h"

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_service.h"

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
    return "/tmp/braillatron-music-self-test-" + std::to_string(::getpid());
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

bool test_scan_builds_artist_album_tree()
{
    const std::string dir = temp_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir + "/music/Beethoven/Symphony 9", ec);
    std::filesystem::create_directories(dir + "/incoming", ec);

    expect_true(write_file(dir + "/music/Beethoven/Symphony 9/movement1.mp3", "fake"), "write track");
    expect_true(write_file(dir + "/music/Beethoven/Symphony 9/movement2.mp3", "fake"), "write track 2");
    expect_true(write_file(dir + "/incoming/imported.flac", "fake"), "write incoming track");

    braillatron::connect::ConnectConfig connect_config;
    connect_config.cookies_incoming_dir = dir + "/incoming";
    connect_config.mpv_socket_path = dir + "/mpv.sock";

    braillatron::connect::MusicConfig music_config;
    music_config.music_dir = dir + "/music";
    music_config.state_path = dir + "/state.json";

    braillatron::connect::MpvService mpv(
        braillatron::connect::MpvService::Options {"mpv", "null", connect_config.mpv_socket_path, ""});
    braillatron::connect::MusicBackend backend(music_config, connect_config, &mpv, nullptr);

    const std::string response = backend.scan();
    expect_true(response.find("\"ok\":true") != std::string::npos, "scan ok");
    expect_true(backend.tracks().size() == 3, "track count includes import");
    expect_true(response.find("Beethoven") != std::string::npos, "artist in scan json");
    expect_true(response.find("Symphony 9") != std::string::npos, "album in scan json");
    expect_true(response.find("imported") != std::string::npos, "imported track in scan json");
    expect_true(std::filesystem::exists(dir + "/music/incoming/imported.flac", ec),
                "incoming audio moved into library");

    bool found_movement = false;
    for (const braillatron::connect::MusicTrack &track : backend.tracks()) {
        if (track.title == "movement1") {
            found_movement = true;
            expect_true(track.artist == "Beethoven", "artist from directory layout");
            expect_true(track.album == "Symphony 9", "album from directory layout");
        }
        if (track.title == "imported") {
            expect_true(track.artist == "Unknown Artist", "imported track artist fallback");
        }
    }
    expect_true(found_movement, "movement track metadata parsed");

    std::filesystem::remove_all(dir, ec);
    return true;
}

} // namespace

int main()
{
    if (!test_scan_builds_artist_album_tree()) {
        ++failures;
    }

    if (failures > 0) {
        std::cerr << failures << " music self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "music self-test passed\n";
    return EXIT_SUCCESS;
}
