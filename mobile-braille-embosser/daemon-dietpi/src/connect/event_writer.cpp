#include "event_writer.h"

#include "json_utils.h"

#include <chrono>
#include <fstream>

namespace braillatron::connect {

EventWriter::EventWriter(std::string path)
    : path_(std::move(path))
{
}

void EventWriter::emit(const std::string &event_type, const std::string &payload_json)
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::string line = "{\"event\":\"" + json_escape(event_type) + "\",\"ts\":" +
                       std::to_string(ms);
    if (!payload_json.empty()) {
        if (payload_json.front() == '{') {
            line += ",\"data\":" + payload_json;
        } else {
            line += ",\"data\":\"" + json_escape(payload_json) + "\"";
        }
    }
    line += "}\n";

    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path_, std::ios::app);
    if (out.is_open()) {
        out << line;
        out.flush();
    }
}

} // namespace braillatron::connect
