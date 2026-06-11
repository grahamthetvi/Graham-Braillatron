#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "subprocess.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace braillatron::connect {

class SignalBackend {
public:
    SignalBackend(MessagesConfig config, EventWriter *events);
    ~SignalBackend();

    bool is_linked() const;
    std::string accounts_status() const;
    std::string start_link();
    std::string finish_link();
    std::string list_chats();
    std::string list_messages(const std::string &recipient);
    std::string send_message(const std::string &recipient, const std::string &text);
    void start_daemon_if_linked();
    void stop_daemon();
    void start_event_thread();
    void stop_event_thread();

private:
    std::string signal_env() const;
    std::string rpc_call(const std::string &method, const std::string &params_json);
    std::string http_post(const std::string &path, const std::string &body);
    std::string http_get(const std::string &path);
    std::string linked_account() const;

    MessagesConfig config_;
    EventWriter *events_;
    ManagedProcess daemon_proc_;
    std::thread event_thread_;
    std::atomic<bool> event_running_ {false};
    std::string pending_link_uri_;
    mutable std::mutex mutex_;
};

} // namespace braillatron::connect
