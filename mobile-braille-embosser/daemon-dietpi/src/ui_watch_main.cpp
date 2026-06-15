#ifdef BRAILLATRON_DISPLAY

#include "ui/display/chrome_snapshot.h"
#include "ui/display/display_config.h"
#include "ui/display/display_ncurses.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::string read_snapshot_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::string payload;
    std::getline(file, payload, '\0');
    if (payload.empty()) {
        std::getline(file, payload);
    }
    return payload;
}

void print_usage(const char *argv0)
{
    std::cerr << "Usage: " << argv0 << " [snapshot-path]\n"
              << "  Live mirror of braillatron-ui chrome over SSH (read-only).\n"
              << "  Default snapshot: /run/braillatron/ui-chrome.snapshot\n"
              << "  Press Ctrl+C to exit. Use the physical keyboard on the Pi for input.\n";
}

} // namespace

int main(int argc, char **argv)
{
    std::string snapshot_path = "/run/braillatron/ui-chrome.snapshot";
    if (argc > 1) {
        if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        snapshot_path = argv[1];
    }

    if (!isatty(STDOUT_FILENO)) {
        std::cerr << "braillatron-ui-watch requires an interactive terminal (use over SSH).\n";
        return 1;
    }

    braillatron::ui::NcursesDisplayBackend terminal;
    if (!terminal.available()) {
        std::cerr << "Failed to initialize terminal UI.\n";
        return 1;
    }

    uint64_t last_sequence = 0;
    bool waiting_message_shown = false;

    while (true) {
        const std::string payload = read_snapshot_file(snapshot_path);
        if (!payload.empty()) {
            braillatron::ui::RenderedChrome frame;
            uint64_t sequence = 0;
            if (braillatron::ui::parse_chrome_snapshot(payload, frame, &sequence) &&
                sequence != last_sequence) {
                last_sequence = sequence;
                waiting_message_shown = false;
                braillatron::ui::render_chrome_terminal(frame);
            }
        } else if (!waiting_message_shown) {
            braillatron::ui::RenderedChrome waiting;
            waiting.header = "Braillatron UI watch";
            waiting.rows = {
                "Waiting for braillatron-ui snapshot...",
                snapshot_path,
                "Ensure braillatron-ui is running and mirror_enabled=true",
            };
            braillatron::ui::render_chrome_terminal(waiting);
            waiting_message_shown = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    return 0;
}

#else

#include <iostream>

int main()
{
    std::cerr << "braillatron-ui-watch requires BRAILLATRON_DISPLAY=1 build.\n";
    return 1;
}

#endif
