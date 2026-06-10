#include "matrix_map.h"

#include "logical_keys.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

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

} // namespace

MatrixMap::MatrixMap()
{
    for (uint16_t &mask : logical_mask_for_physical_) {
        mask = 0u;
    }
}

MatrixMap MatrixMap::load(const std::string &path)
{
    MatrixMap map;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open matrix map config: " + path);
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

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key.size() != 6 || key.rfind("map_", 0) != 0) {
            continue;
        }

        const int index = std::stoi(key.substr(4));
        if (index < 0 || index > 15) {
            throw std::runtime_error("matrix map index out of range: " + key);
        }

        map.logical_mask_for_physical_[static_cast<size_t>(index)] =
            logical_mask_from_name(value);
    }

    return map;
}

uint16_t MatrixMap::remap(uint16_t physical_state) const
{
    uint16_t logical = 0u;

    for (size_t physical_bit = 0; physical_bit < 16; ++physical_bit) {
        if ((physical_state & (uint16_t)(1u << physical_bit)) == 0u) {
            continue;
        }

        logical |= logical_mask_for_physical_[physical_bit];
    }

    return logical;
}

} // namespace braillatron::keyboard
