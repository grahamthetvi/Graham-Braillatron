#pragma once

#include "app_session.h"

#include <memory>

namespace braillatron::ui {

std::unique_ptr<AppSession> make_brailler_app();
std::unique_ptr<AppSession> make_calculator_app();
std::unique_ptr<AppSession> make_transcriber_app();
std::unique_ptr<AppSession> make_morse_learn_app();
std::unique_ptr<AppSession> make_network_app();
std::unique_ptr<AppSession> make_library_app();
std::unique_ptr<AppSession> make_localsend_app();
std::unique_ptr<AppSession> make_wikipedia_app();
std::unique_ptr<AppSession> make_quick_status_inline();
std::unique_ptr<AppSession> make_morse_output_inline();
std::unique_ptr<AppSession> make_paper_nav_inline();
std::unique_ptr<AppSession> make_save_exit_inline();

} // namespace braillatron::ui
