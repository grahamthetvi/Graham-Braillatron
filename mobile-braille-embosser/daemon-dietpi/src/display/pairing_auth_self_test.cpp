#include "pairing_auth.h"

#include <iostream>

int run_pairing_auth_self_test()
{
    braillatron::display::PairingAuth auth(30);
    const std::string code = auth.start_pairing();
    if (code.size() != 6) {
        std::cerr << "pairing code length mismatch\n";
        return 1;
    }

    const auto token = auth.verify_pairing(code);
    if (!token.has_value()) {
        std::cerr << "pairing verification failed\n";
        return 1;
    }

    if (!auth.validate_session(*token).has_value()) {
        std::cerr << "session validation failed\n";
        return 1;
    }

    for (int i = 0; i < 5; ++i) {
        if (auth.verify_pairing("000000").has_value()) {
            std::cerr << "unexpected pairing success\n";
            return 1;
        }
    }
    if (!auth.rate_limited()) {
        std::cerr << "rate limit not engaged\n";
        return 1;
    }

    std::cout << "pairing auth self-test passed\n";
    return 0;
}
