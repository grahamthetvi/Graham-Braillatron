#include "mpv_service.h"

#include "subprocess.h"

#include <chrono>
#include <thread>

namespace braillatron::connect {

MpvService::MpvService(Options options)
    : options_(std::move(options))
    , mpv_(options_.socket_path)
{
}

bool MpvService::wait_for_socket(int attempts, int delay_ms) const
{
    for (int i = 0; i < attempts; ++i) {
        if (file_exists(options_.socket_path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return file_exists(options_.socket_path);
}

bool MpvService::ensure_started()
{
    if (mpv_proc_.pid > 0) {
        return wait_for_socket(5, 20);
    }
    const std::string cmd = options_.mpv_path + " --idle=yes --no-video --ao=" + options_.mpv_ao +
                            " --input-ipc-server=" + options_.socket_path + options_.extra_args +
                            " >/dev/null 2>&1";
    mpv_proc_ = spawn_background(cmd);
    if (mpv_proc_.pid <= 0) {
        return false;
    }
    return wait_for_socket(50, 100);
}

void MpvService::stop()
{
    mpv_.stop();
    mpv_proc_.stop();
    paused_ = false;
}

bool MpvService::pause_toggle()
{
    return set_paused(!paused_);
}

bool MpvService::set_paused(bool pause)
{
    if (!ensure_started()) {
        return false;
    }
    if (pause == paused_) {
        return true;
    }
    paused_ = pause;
    return pause ? mpv_.pause() : mpv_.resume();
}

void MpvService::mark_playing()
{
    paused_ = false;
}

} // namespace braillatron::connect
