#pragma once

#include <string>
#include <vector>

namespace braillatron::connect {

std::string json_escape(const std::string &value);
std::string json_get_string(const std::string &json, const std::string &key);
bool json_get_bool(const std::string &json, const std::string &key, bool default_value);
std::vector<std::string> json_split_objects(const std::string &array_json);

} // namespace braillatron::connect
