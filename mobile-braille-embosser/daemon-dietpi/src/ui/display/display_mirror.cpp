#include "display_mirror.h"

#include "chrome_renderer.h"
#include "chrome_snapshot.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace braillatron::ui {

namespace {

bool ensure_parent_dir(const std::string &path)
{
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    return !error;
}

bool atomic_write_file(const std::string &path, const std::string &payload)
{
    const std::string temp_path = path + ".tmp";
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << payload;
        if (!file.good()) {
            unlink(temp_path.c_str());
            return false;
        }
    }
    if (rename(temp_path.c_str(), path.c_str()) != 0) {
        unlink(temp_path.c_str());
        return false;
    }
    chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    return true;
}

} // namespace

MirrorDisplayBackend::MirrorDisplayBackend(DisplayConfig config)
    : config_(std::move(config))
{
    ready_ = ensure_parent_dir(config_.mirror_snapshot);
}

bool MirrorDisplayBackend::available() const
{
    return ready_;
}

void MirrorDisplayBackend::render(const UiChromeModel &model)
{
    if (!ready_) {
        return;
    }

    ChromeRenderer renderer(24);
    publish_snapshot(renderer.build(model));
}

bool MirrorDisplayBackend::publish_snapshot(const RenderedChrome &frame)
{
    ++sequence_;
    const std::string payload = serialize_chrome_snapshot(frame, sequence_);
    if (!atomic_write_file(config_.mirror_snapshot, payload)) {
        std::cerr << "[display] mirror write failed: " << config_.mirror_snapshot
                  << " (" << std::strerror(errno) << ")\n";
        ready_ = false;
        return false;
    }
    return true;
}

void MirrorDisplayBackend::shutdown()
{
    ready_ = false;
}

std::string MirrorDisplayBackend::backend_label() const
{
    return "mirror";
}

MirrorDisplayBackend *try_create_mirror(const DisplayConfig &config)
{
    if (!config.mirror_enabled || config.mirror_snapshot.empty()) {
        return nullptr;
    }

    auto *backend = new MirrorDisplayBackend(config);
    if (!backend->available()) {
        delete backend;
        return nullptr;
    }
    return backend;
}

} // namespace braillatron::ui
