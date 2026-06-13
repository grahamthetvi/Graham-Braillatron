#ifdef BRAILLATRON_DISPLAY

#include "display_ncurses.h"

#include <curses.h>
#include <string>

namespace braillatron::ui {

namespace {

std::string status_suffix(const RenderedChrome &frame)
{
    std::string suffix;
    if (frame.tts_paused) {
        suffix += " [TTS paused]";
    }
    if (frame.dictation_active) {
        suffix += " [Mic]";
    }
    return suffix;
}

} // namespace

NcursesDisplayBackend::NcursesDisplayBackend()
{
    if (initscr() == nullptr) {
        return;
    }

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_YELLOW, COLOR_BLACK);
        init_pair(2, COLOR_WHITE, COLOR_BLUE);
    }
    initialized_ = true;
}

NcursesDisplayBackend::~NcursesDisplayBackend()
{
    shutdown();
}

bool NcursesDisplayBackend::available() const
{
    return initialized_;
}

void NcursesDisplayBackend::render(const RenderedChrome &frame)
{
    if (!initialized_) {
        return;
    }

    erase();

    int row = 0;
    const int max_cols = COLS > 0 ? COLS : 80;

    std::string header = frame.header + status_suffix(frame);
    if (static_cast<int>(header.size()) > max_cols) {
        header = header.substr(0, static_cast<size_t>(max_cols));
    }
    attron(A_BOLD);
    mvprintw(row++, 0, "%s", header.c_str());
    attroff(A_BOLD);

    if (!frame.breadcrumb.empty()) {
        std::string crumb = frame.breadcrumb;
        if (static_cast<int>(crumb.size()) > max_cols) {
            crumb = crumb.substr(0, static_cast<size_t>(max_cols));
        }
        mvprintw(row++, 0, "%s", crumb.c_str());
    }

    if (frame.at_top_boundary) {
        mvprintw(row++, 0, "-- top --");
    }

    for (size_t i = 0; i < frame.rows.size(); ++i) {
        std::string line = frame.rows[i];
        if (static_cast<int>(line.size()) > max_cols - 2) {
            line = line.substr(0, static_cast<size_t>(max_cols - 5)) + "...";
        }

        if (i == frame.focus_row) {
            if (has_colors()) {
                attron(COLOR_PAIR(2) | A_BOLD);
            } else {
                attron(A_REVERSE | A_BOLD);
            }
            mvprintw(row, 0, "> %s", line.c_str());
            if (has_colors()) {
                attroff(COLOR_PAIR(2) | A_BOLD);
            } else {
                attroff(A_REVERSE | A_BOLD);
            }
        } else {
            mvprintw(row, 0, "  %s", line.c_str());
        }
        ++row;
    }

    if (frame.at_bottom_boundary) {
        mvprintw(row++, 0, "-- bottom --");
    }

    if (LINES > 1) {
        const int toast_row = LINES - 1;
        move(toast_row, 0);
        clrtoeol();
        if (!frame.toast.empty()) {
            std::string toast = frame.toast;
            if (static_cast<int>(toast.size()) > max_cols) {
                toast = toast.substr(0, static_cast<size_t>(max_cols));
            }
            if (has_colors()) {
                attron(COLOR_PAIR(1));
            }
            mvprintw(toast_row, 0, "%s", toast.c_str());
            if (has_colors()) {
                attroff(COLOR_PAIR(1));
            }
        }
    }

    refresh();
}

void NcursesDisplayBackend::shutdown()
{
    if (!initialized_) {
        return;
    }
    endwin();
    initialized_ = false;
}

} // namespace braillatron::ui

#endif // BRAILLATRON_DISPLAY
