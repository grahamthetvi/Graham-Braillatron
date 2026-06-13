#include "ui_chrome_model.h"

#include "../menu_overlay.h"

namespace braillatron::ui {

std::string resolve_menu_item_label(const MenuItem &item)
{
    if (item.dynamic_label) {
        return item.dynamic_label();
    }
    return item.label;
}

} // namespace braillatron::ui
