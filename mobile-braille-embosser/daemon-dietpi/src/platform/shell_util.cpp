#include "shell_util.h"

#include <array>
#include <cstdio>
#include <string>

namespace braillatron::platform {

std::string run_command(const std::string &cmd)
{
    std::array<char, 256> buffer {};
    std::string result;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

} // namespace braillatron::platform
