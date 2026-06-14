#include "host_chord_assembler.h"

extern "C" {
#include "protocol.h"
}

#include <iostream>
#include <vector>

namespace {

struct MatrixEvent {
    uint16_t key_state;
};

std::vector<MatrixEvent> g_matrix_events;
std::vector<uint8_t> g_chord_events;

void reset_events()
{
    g_matrix_events.clear();
    g_chord_events.clear();
}

} // namespace

int main()
{
    using namespace braillatron::keyboard;

    HostChordAssembler assembler;
    assembler.set_keyboard_matrix_handler([](uint16_t key_state) {
        g_matrix_events.push_back({key_state});
    });
    assembler.set_chord_handler([](uint8_t dot_mask) { g_chord_events.push_back(dot_mask); });

    // Control keys bypass chord assembly.
    reset_events();
    assembler.reset();
    assembler.update(BRAILLATRON_KEY_DPAD_UP, true, 0);
    if (g_matrix_events.size() != 1 || g_matrix_events[0].key_state != BRAILLATRON_KEY_DPAD_UP) {
        std::cerr << "host chord self-test failed: control key matrix event\n";
        return 1;
    }
    if (!g_chord_events.empty()) {
        std::cerr << "host chord self-test failed: unexpected chord on control key\n";
        return 1;
    }

    // Single dot commits on release, not while held.
    reset_events();
    assembler.reset();
    assembler.update(BRAILLATRON_KEY_DOT_1, true, 0);
    if (!g_matrix_events.empty()) {
        std::cerr << "host chord self-test failed: dot press should not emit matrix\n";
        return 1;
    }
    if (!g_chord_events.empty()) {
        std::cerr << "host chord self-test failed: chord fired before release\n";
        return 1;
    }
    assembler.update(0, true, 10);
    if (g_chord_events.size() != 1 || g_chord_events[0] != BRAILLATRON_KEY_DOT_1) {
        std::cerr << "host chord self-test failed: single-dot chord mismatch\n";
        return 1;
    }

    // Multiple dots within one chord are OR'd and commit when all are released.
    reset_events();
    assembler.reset();
    assembler.update(BRAILLATRON_KEY_DOT_1, true, 100);
    assembler.update(BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_3, true, 110);
    if (!g_chord_events.empty()) {
        std::cerr << "host chord self-test failed: multi-dot chord fired before release\n";
        return 1;
    }
    assembler.update(BRAILLATRON_KEY_DOT_3, true, 120);
    assembler.update(0, true, 130);
    const uint8_t expected_chord =
        static_cast<uint8_t>(BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_3);
    if (g_chord_events.size() != 1 || g_chord_events[0] != expected_chord) {
        std::cerr << "host chord self-test failed: multi-dot chord mismatch\n";
        return 1;
    }

    // Stuck-key safety net auto-commits after the hold timeout.
    reset_events();
    assembler.reset();
    assembler.update(BRAILLATRON_KEY_DOT_2, true, 1000);
    assembler.update(BRAILLATRON_KEY_DOT_2, false, 2999);
    if (!g_chord_events.empty()) {
        std::cerr << "host chord self-test failed: chord fired before timeout expiry\n";
        return 1;
    }
    assembler.update(BRAILLATRON_KEY_DOT_2, false, 3001);
    if (g_chord_events.size() != 1 || g_chord_events[0] != BRAILLATRON_KEY_DOT_2) {
        std::cerr << "host chord self-test failed: timeout chord mismatch\n";
        return 1;
    }

    std::cout << "host chord self-test ok\n";
    return 0;
}
