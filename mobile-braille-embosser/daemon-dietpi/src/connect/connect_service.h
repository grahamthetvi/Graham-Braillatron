#pragma once

#include "connect_config.h"
#include "connect_job_queue.h"
#include "event_writer.h"
#include "signal_backend.h"
#include "socket_server.h"
#include "youtube_backend.h"

#include <atomic>
#include <cstdint>

namespace braillatron::connect {

class ConnectService {
public:
    ConnectService(ConnectConfig connect_config, YoutubeConfig youtube_config,
                   MessagesConfig messages_config);

    void start();
    void stop();
    void poll();

private:
    std::string handle_request(const std::string &request);
    std::string execute_command(const std::string &cmd, const std::string &request);
    std::string cmd_from_request(const std::string &request) const;

    ConnectConfig connect_config_;
    EventWriter events_;
    YoutubeBackend youtube_;
    SignalBackend signal_;
    SocketServer server_;
    ConnectJobQueue jobs_;
    std::atomic<bool> running_ {false};
    uint64_t last_cookie_poll_ms_ = 0;
};

} // namespace braillatron::connect
