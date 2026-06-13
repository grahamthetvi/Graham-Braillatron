#pragma once

#include "event_writer.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace braillatron::connect {

struct ConnectJob {
    std::string request_id;
    std::function<void(EventWriter *events)> run;
};

class ConnectJobQueue {
public:
    ConnectJobQueue() = default;
    ~ConnectJobQueue();

    ConnectJobQueue(const ConnectJobQueue &) = delete;
    ConnectJobQueue &operator=(const ConnectJobQueue &) = delete;

    void start(EventWriter *events);
    void stop();
    void submit(ConnectJob job);

private:
    void worker_loop();

    EventWriter *events_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<ConnectJob> jobs_;
    std::atomic<bool> running_ {false};
};

} // namespace braillatron::connect
