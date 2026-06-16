#pragma once

#include <string>

namespace braillatron::ui {

struct AccessibleElement {
    std::string role;
    std::string name;
    std::string value;
    std::string state;

    // Optional context metadata for context-aware verbosity
    std::string container;
    int index = -1;      // 0-indexed position, -1 if not applicable
    int count = -1;      // total items in list, -1 if not applicable
};

class IAccessibleOutput {
public:
    virtual ~IAccessibleOutput() = default;

    // Speak a structured accessibility node/element (RNVS metadata)
    virtual void announce_element(const AccessibleElement &element) = 0;

    // Speak a generic text message (alerts, system status, etc.)
    virtual void announce_message(const std::string &message) = 0;

    // Immediately stop current speech output
    virtual void stop() = 0;
};

} // namespace braillatron::ui
