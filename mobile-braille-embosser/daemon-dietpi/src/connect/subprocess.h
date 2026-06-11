#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace braillatron::connect {

std::string run_command(const std::string &cmd);
int run_command_status(const std::string &cmd);
bool file_exists(const std::string &path);
bool atomic_move_file(const std::string &from, const std::string &to);
void ensure_directory(const std::string &path);

struct ManagedProcess {
    pid_t pid = -1;
    void stop();
};

ManagedProcess spawn_background(const std::string &cmd);

} // namespace braillatron::connect
