#include "connect_async.h"

#include "json_utils.h"

#include <atomic>
#include <chrono>
#include <sstream>

namespace braillatron::connect {

namespace {

uint64_t steady_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

std::string generate_request_id()
{
    static std::atomic<uint32_t> counter {0};
    std::ostringstream out;
    out << "req-" << steady_ms() << '-' << counter.fetch_add(1);
    return out.str();
}

std::string request_id_from_json(const std::string &request)
{
    const std::string existing = json_get_string(request, "request_id");
    return existing.empty() ? generate_request_id() : existing;
}

bool is_async_command(const std::string &cmd)
{
    return cmd == "signal.start_link" || cmd == "signal.finish_link" || cmd == "signal.list_chats" ||
           cmd == "signal.list_messages" || cmd == "signal.send" || cmd == "youtube.search" ||
           cmd == "music.scan" || cmd == "weather.fetch" || cmd == "weather.set_location" ||
           cmd == "weather.set_temperature_unit" || cmd == "podcasts.refresh" ||
           cmd == "podcasts.download" || cmd == "radio.search" || cmd == "library.search" ||
           cmd == "library.download" || cmd == "gmail.start_link" || cmd == "gmail.list_inbox" ||
           cmd == "gmail.read_message" || cmd == "gmail.send" || cmd == "gmail.reply" ||
           cmd == "gmail.archive" || cmd == "gmail.delete" || cmd == "gmail.star";
}

std::string make_pending_response(const std::string &request_id)
{
    return "{\"ok\":true,\"pending\":true,\"request_id\":\"" + json_escape(request_id) + "\"}";
}

void emit_connect_response(EventWriter *events, const std::string &request_id,
                           const std::string &result_json)
{
    if (events == nullptr) {
        return;
    }
    std::string payload = result_json;
    if (payload.empty()) {
        payload = "{}";
    }
    if (payload.front() != '{') {
        payload = "{\"value\":\"" + json_escape(payload) + "\"}";
    }
    if (payload.find("\"request_id\"") == std::string::npos) {
        payload.insert(1, "\"request_id\":\"" + json_escape(request_id) + "\",");
    }
    events->emit("connect.response", payload);
}

} // namespace braillatron::connect
