#include "telemetry_bridge.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::telemetry {

namespace fs = std::filesystem;

bool write_telemetry_json(const std::string &path, const TelemetrySnapshot &snapshot)
{
    const fs::path file_path(path);
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }

    const std::string temp_path = path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output << "{\n";
        output << "  \"battery_percent\": " << static_cast<unsigned>(snapshot.battery_percent)
               << ",\n";
        output << "  \"temperature_c\": " << static_cast<int>(snapshot.temperature_c) << ",\n";
        output << "  \"battery_mv\": " << snapshot.battery_mv << ",\n";
        output << "  \"limit_status\": " << static_cast<unsigned>(snapshot.limit_status)
               << ",\n";
        output << "  \"motion_blocked\": " << (snapshot.motion_blocked ? "true" : "false")
               << "\n";
        output << "}\n";
        output.flush();
        if (!output.good()) {
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp_path, path, ec);
    return !ec;
}

TelemetrySnapshot read_telemetry_json(const std::string &path)
{
    TelemetrySnapshot snapshot {};
    std::ifstream input(path);
    if (!input.is_open()) {
        return snapshot;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();

    auto parse_uint = [&](const char *key, uint8_t *out) {
        const std::string needle = std::string("\"") + key + "\":";
        const size_t pos = json.find(needle);
        if (pos == std::string::npos) {
            return;
        }
        *out = static_cast<uint8_t>(std::strtoul(json.c_str() + pos + needle.size(), nullptr, 10));
    };

    parse_uint("battery_percent", &snapshot.battery_percent);
    int temp = 0;
    const std::string temp_key = "\"temperature_c\":";
    const size_t temp_pos = json.find(temp_key);
    if (temp_pos != std::string::npos) {
        temp = std::atoi(json.c_str() + temp_pos + temp_key.size());
        snapshot.temperature_c = static_cast<int8_t>(temp);
    }

    const std::string mv_key = "\"battery_mv\":";
    const size_t mv_pos = json.find(mv_key);
    if (mv_pos != std::string::npos) {
        snapshot.battery_mv =
            static_cast<uint16_t>(std::strtoul(json.c_str() + mv_pos + mv_key.size(), nullptr, 10));
    }

    snapshot.motion_blocked = json.find("\"motion_blocked\": true") != std::string::npos;
    return snapshot;
}

} // namespace braillatron::telemetry
