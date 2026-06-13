#pragma once

#include <string>

namespace braillatron::connect {

class MpvIpc {
public:
    explicit MpvIpc(std::string socket_path);

    bool send_command(const std::string &command_json);
    bool load_url(const std::string &url);
    bool seek_seconds(double seconds);
    bool pause();
    bool resume();
    bool stop();
    bool is_playing() const { return playing_; }

private:
    std::string socket_path_;
    bool playing_ = false;
};

} // namespace braillatron::connect
