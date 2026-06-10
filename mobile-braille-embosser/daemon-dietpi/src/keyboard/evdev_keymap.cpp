#include "evdev_keymap.h"

#include "logical_keys.h"

#include <cctype>
#include <fstream>
#include <linux/input-event-codes.h>
#include <stdexcept>
#include <unordered_map>

namespace braillatron::keyboard {

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string to_upper(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

const std::unordered_map<std::string, unsigned> &evdev_name_table()
{
    static const std::unordered_map<std::string, unsigned> table = {
        {"KEY_F1", KEY_F1},
        {"KEY_F2", KEY_F2},
        {"KEY_F3", KEY_F3},
        {"KEY_F4", KEY_F4},
        {"KEY_F5", KEY_F5},
        {"KEY_F6", KEY_F6},
        {"KEY_F7", KEY_F7},
        {"KEY_F8", KEY_F8},
        {"KEY_F9", KEY_F9},
        {"KEY_F10", KEY_F10},
        {"KEY_F11", KEY_F11},
        {"KEY_F12", KEY_F12},
        {"KEY_S", KEY_S},
        {"KEY_D", KEY_D},
        {"KEY_F", KEY_F},
        {"KEY_J", KEY_J},
        {"KEY_K", KEY_K},
        {"KEY_L", KEY_L},
        {"KEY_UP", KEY_UP},
        {"KEY_DOWN", KEY_DOWN},
        {"KEY_LEFT", KEY_LEFT},
        {"KEY_RIGHT", KEY_RIGHT},
        {"KEY_BACKSPACE", KEY_BACKSPACE},
        {"KEY_ENTER", KEY_ENTER},
        {"KEY_KPENTER", KEY_KPENTER},
        {"KEY_SPACE", KEY_SPACE},
        {"KEY_LEFTMETA", KEY_LEFTMETA},
        {"KEY_RIGHTMETA", KEY_RIGHTMETA},
        {"KEY_ESC", KEY_ESC},
        {"KEY_DELETE", KEY_DELETE},
        {"KEY_HOME", KEY_HOME},
        {"KEY_END", KEY_END},
        {"KEY_PAGEUP", KEY_PAGEUP},
        {"KEY_PAGEDOWN", KEY_PAGEDOWN},
        {"KEY_TAB", KEY_TAB},
        {"KEY_1", KEY_1},
        {"KEY_2", KEY_2},
        {"KEY_3", KEY_3},
        {"KEY_4", KEY_4},
        {"KEY_5", KEY_5},
        {"KEY_6", KEY_6},
    };
    return table;
}

} // namespace

unsigned evdev_code_from_name(const std::string &name)
{
    const std::string key = to_upper(trim(name));
    const auto &table = evdev_name_table();
    const auto it = table.find(key);
    if (it == table.end()) {
        throw std::runtime_error("unknown evdev key name: " + name);
    }
    return it->second;
}

EvdevKeymap EvdevKeymap::load(const std::string &path)
{
    EvdevKeymap map;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open evdev map config: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string evdev_name = trim(line.substr(0, eq));
        const std::string logical_name = trim(line.substr(eq + 1));
        const unsigned code = evdev_code_from_name(evdev_name);
        const uint16_t mask = logical_mask_from_name(logical_name);
        if (mask != 0) {
            map.code_to_mask_[code] = mask;
        }
    }

    return map;
}

uint16_t EvdevKeymap::logical_mask_for_code(unsigned evdev_code) const
{
    const auto it = code_to_mask_.find(evdev_code);
    if (it == code_to_mask_.end()) {
        return 0;
    }
    return it->second;
}

bool EvdevKeymap::has_mapping(unsigned evdev_code) const
{
    return code_to_mask_.find(evdev_code) != code_to_mask_.end();
}

} // namespace braillatron::keyboard
