#include "connect_job_queue.h"

namespace braillatron::connect {

ConnectJobQueue::~ConnectJobQueue()
{
    stop();
}

void ConnectJobQueue::start(EventWriter *events)
{
    if (running_.load()) {
        return;
    }
    events_ = events;
    running_ = true;
    worker_ = std::thread([this]() { worker_loop(); });
}

void ConnectJobQueue::stop()
{
    if (!running_.load()) {
        return;
    }
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    events_ = nullptr;
}

void ConnectJobQueue::submit(ConnectJob job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
}

void ConnectJobQueue::worker_loop()
{
    while (running_.load()) {
        ConnectJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !running_.load() || !jobs_.empty(); });
            if (!running_.load() && jobs_.empty()) {
                return;
            }
            if (jobs_.empty()) {
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        if (job.run && events_ != nullptr) {
            job.run(events_);
        }
    }
}

} // namespace braillatron::connect
