#include "subprocess.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace braillatron::connect {

std::string run_command(const std::string &cmd)
{
    std::string output;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return output;
    }
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    pclose(pipe);
    return output;
}

int run_command_status(const std::string &cmd)
{
    return std::system(cmd.c_str());
}

bool file_exists(const std::string &path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

void ensure_directory(const std::string &path)
{
    if (path.empty()) {
        return;
    }
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/' && !current.empty()) {
            mkdir(current.c_str(), 0700);
        }
        current += path[i];
    }
    if (!current.empty()) {
        mkdir(current.c_str(), 0700);
    }
}

bool atomic_move_file(const std::string &from, const std::string &to)
{
    if (!file_exists(from)) {
        return false;
    }
    const std::string tmp = to + ".tmp";
    {
        std::ifstream in(from, std::ios::binary);
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!in.is_open() || !out.is_open()) {
            return false;
        }
        out << in.rdbuf();
        out.flush();
    }
    if (rename(tmp.c_str(), to.c_str()) != 0) {
        return false;
    }
    unlink(from.c_str());
    return true;
}

void ManagedProcess::stop()
{
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        pid = -1;
    }
}

ManagedProcess spawn_background(const std::string &cmd)
{
    ManagedProcess proc;
    const pid_t child = fork();
    if (child == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    if (child > 0) {
        proc.pid = child;
    }
    return proc;
}

} // namespace braillatron::connect
