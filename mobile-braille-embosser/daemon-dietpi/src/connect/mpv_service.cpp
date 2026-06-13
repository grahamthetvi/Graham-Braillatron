#include "mpv_service.h"

namespace braillatron::connect {

MpvService::MpvService(Options options)
    : options_(std::move(options))
    , mpv_(options_.socket_path)
{
}

bool MpvService::ensure_started()
{
    if (mpv_proc_.pid > 0) {
        return true;
    }
    const std::string cmd = options_.mpv_path + " --idle=yes --no-video --ao=" + options_.mpv_ao +
                            " --input-ipc-server=" + options_.socket_path + options_.extra_args +
                            " >/dev/null 2>&1";
    mpv_proc_ = spawn_background(cmd);
    return mpv_proc_.pid > 0;
}

void MpvService::stop()
{
    mpv_.stop();
    mpv_proc_.stop();
    paused_ = false;
}

bool MpvService::pause_toggle()
{
    if (paused_) {
        paused_ = false;
        return mpv_.resume();
    }
    paused_ = true;
    return mpv_.pause();
}

void MpvService::mark_playing()
{
    paused_ = false;
}

} // namespace braillatron::connect
