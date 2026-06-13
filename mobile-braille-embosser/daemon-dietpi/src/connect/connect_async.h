#pragma once

#include "event_writer.h"

#include <string>

namespace braillatron::connect {

std::string generate_request_id();
std::string request_id_from_json(const std::string &request);
bool is_async_command(const std::string &cmd);
std::string make_pending_response(const std::string &request_id);
void emit_connect_response(EventWriter *events, const std::string &request_id,
                           const std::string &result_json);

} // namespace braillatron::connect
