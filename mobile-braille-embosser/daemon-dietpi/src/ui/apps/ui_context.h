#pragma once

#include "../../documents/brf_store.h"
#include "../../documents/coordinate_state.h"
#include "../../documents/edit_session.h"
#include "../../documents/liblouis_bridge.h"
#include "../../documents/paper_separator.h"
#include "../../motion/motion_service.h"

#include <string>

namespace braillatron::connect {
class ConnectClient;
}

namespace braillatron::ui {

class AppRegistry;
class OutputHub;
class TimerService;

struct UiContext {
    OutputHub *output = nullptr;
    motion::MotionService *motion = nullptr;
    documents::BrfStore *brf = nullptr;
    documents::CoordinateStore *coords = nullptr;
    documents::EditSession *edit = nullptr;
    documents::PaperSeparator *paper_sep = nullptr;
    documents::BrailleTranslationService *braille = nullptr;
    documents::BrailleTranslationService *braille_input = nullptr;
    AppRegistry *registry = nullptr;
    connect::ConnectClient *connect = nullptr;
    TimerService *timer = nullptr;
};

} // namespace braillatron::ui
