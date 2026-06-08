#pragma once

#include "telemetry_config.h"

#include <string>
#include <vector>

namespace braillatron::telemetry {

class RamTextPersistence {
public:
    explicit RamTextPersistence(TelemetryConfig config);

    bool persist_layers_transactional() const;

private:
    TelemetryConfig config_;

    bool persist_single_layer(const std::string &ram_path, const std::string &dest_path) const;
    static bool fsync_path(const std::string &path);
};

} // namespace braillatron::telemetry
