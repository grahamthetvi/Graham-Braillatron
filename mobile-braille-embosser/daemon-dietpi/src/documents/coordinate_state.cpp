#include "coordinate_state.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::documents {

namespace fs = std::filesystem;

namespace {

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 4);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

} // namespace

CoordinateStore::CoordinateStore(std::string ram_path)
    : ram_path_(std::move(ram_path))
{
}

bool CoordinateStore::load()
{
    std::ifstream input(ram_path_);
    if (!input.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();

    auto parse_string = [&](const char *key, std::string *out) {
        const std::string needle = std::string("\"") + key + "\":\"";
        const size_t pos = json.find(needle);
        if (pos == std::string::npos) {
            return;
        }
        const size_t start = pos + needle.size();
        const size_t end = json.find('"', start);
        if (end != std::string::npos) {
            *out = json.substr(start, end - start);
        }
    };

    auto parse_int = [&](const char *key, int64_t *out) {
        const std::string needle = std::string("\"") + key + "\":";
        const size_t pos = json.find(needle);
        if (pos == std::string::npos) {
            return;
        }
        const size_t start = pos + needle.size();
        *out = std::strtoll(json.c_str() + start, nullptr, 10);
    };

    parse_int("x_microsteps", &state_.x_microsteps);
    int64_t y = 0;
    parse_int("y_line_index", &y);
    state_.y_line_index = static_cast<int32_t>(y);
    parse_string("active_app_id", &state_.active_app_id);
    parse_string("brf_path", &state_.brf_path);
    return true;
}

bool CoordinateStore::save() const
{
    const fs::path path(ram_path_);
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }

    const std::string temp_path = ram_path_ + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output << "{\n";
        output << "  \"x_microsteps\": " << state_.x_microsteps << ",\n";
        output << "  \"y_line_index\": " << state_.y_line_index << ",\n";
        output << "  \"active_app_id\": \"" << json_escape(state_.active_app_id) << "\",\n";
        output << "  \"brf_path\": \"" << json_escape(state_.brf_path) << "\"\n";
        output << "}\n";
        output.flush();
        if (!output.good()) {
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp_path, ram_path_, ec);
    return !ec;
}

} // namespace braillatron::documents
