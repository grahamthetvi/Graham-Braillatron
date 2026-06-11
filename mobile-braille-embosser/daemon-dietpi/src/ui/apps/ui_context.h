#pragma once

#include "../../documents/brf_store.h"
#include "../../documents/coordinate_state.h"
#include "../../documents/edit_session.h"
#include "../../documents/paper_separator.h"
#include "../../motion/motion_service.h"

#include <string>

namespace braillatron::ui {

class AppRegistry;
class OutputHub;

struct UiContext {
    OutputHub *output = nullptr;
    motion::MotionService *motion = nullptr;
    documents::BrfStore *brf = nullptr;
    documents::CoordinateStore *coords = nullptr;
    documents::EditSession *edit = nullptr;
    documents::PaperSeparator *paper_sep = nullptr;
    AppRegistry *registry = nullptr;
};

} // namespace braillatron::ui
