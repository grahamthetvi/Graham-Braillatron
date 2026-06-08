#include "ram_text_persistence.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace braillatron::telemetry {

namespace fs = std::filesystem;

RamTextPersistence::RamTextPersistence(TelemetryConfig config)
    : config_(std::move(config))
{
}

bool RamTextPersistence::fsync_path(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

bool RamTextPersistence::persist_single_layer(const std::string &ram_path,
                                              const std::string &dest_path) const
{
    std::ifstream input(ram_path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    const std::string temp_path = dest_path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << input.rdbuf();
        output.flush();
        if (!output.good()) {
            return false;
        }
    }

    if (!fsync_path(temp_path)) {
        return false;
    }

    fs::rename(temp_path, dest_path);
    return fsync_path(dest_path);
}

bool RamTextPersistence::persist_layers_transactional() const
{
    if (config_.ram_text_layers.empty()) {
        return true;
    }

    std::error_code ec;
    fs::create_directories(config_.persistent_output_dir, ec);

    for (size_t i = 0; i < config_.ram_text_layers.size(); ++i) {
        const std::string &ram_path = config_.ram_text_layers[i];
        const std::string dest_path =
            config_.persistent_output_dir + "/layer" + std::to_string(i) + ".brf";

        if (!persist_single_layer(ram_path, dest_path)) {
            return false;
        }
    }

    return true;
}

} // namespace braillatron::telemetry
