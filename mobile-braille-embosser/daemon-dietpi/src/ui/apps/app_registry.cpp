#include "app_registry.h"

#include "all_apps.h"
#include "../output_hub.h"

namespace braillatron::ui {

AppRegistry::AppRegistry()
{
    register_app(make_brailler_app());
    register_app(make_calculator_app());
    register_app(make_transcriber_app());
    register_app(make_morse_learn_app());
    register_app(make_network_app());
    register_app(make_youtube_app());
    register_app(make_messages_app());
    register_app(make_library_app());
    register_app(make_localsend_app());
    register_app(make_quick_status_inline());
    register_app(make_morse_output_inline());
    register_app(make_paper_nav_inline());
    register_app(make_save_exit_inline());
}

void AppRegistry::register_app(std::unique_ptr<AppSession> app)
{
    apps_.push_back(std::move(app));
}

void AppRegistry::set_context(UiContext ctx)
{
    ctx_ = ctx;
    if (ctx_.registry == nullptr) {
        ctx_.registry = this;
    }
}

bool AppRegistry::enter(const std::string &id)
{
    for (const auto &app : apps_) {
        if (app->id() == id && app->kind() == AppKind::Standalone) {
            if (active_ != nullptr) {
                active_->on_exit(ctx_);
            }
            active_ = app.get();
            active_->on_enter(ctx_);
            return true;
        }
    }
    return false;
}

void AppRegistry::exit()
{
    if (active_ != nullptr) {
        active_->on_exit(ctx_);
        active_ = nullptr;
    }
}

bool AppRegistry::switch_app(const std::string &id)
{
    if (ctx_.paper_sep != nullptr) {
        ctx_.paper_sep->separate_to_fresh_page();
    }
    return enter(id);
}

void AppRegistry::poll()
{
    if (active_ != nullptr) {
        active_->on_poll(ctx_);
    }
}

void AppRegistry::on_chord(uint8_t dot_mask)
{
    if (active_ != nullptr) {
        active_->on_chord(dot_mask, ctx_);
    }
}

void AppRegistry::on_text(const std::string &text)
{
    if (active_ != nullptr) {
        active_->on_text(text, ctx_);
    }
}

void AppRegistry::on_control(keyboard::ControlKey key, bool pressed)
{
    if (active_ != nullptr) {
        active_->on_control(key, pressed, ctx_);
    }
}

std::vector<MenuItem> AppRegistry::build_launcher_menu()
{
    std::vector<MenuItem> items;
    for (const auto &app : apps_) {
        if (app->kind() != AppKind::Standalone) {
            continue;
        }
        const std::string app_id = app->id();
        items.push_back(MenuItem {
            app->label(),
            {},
            [this, app_id](MenuOverlay &mo) {
                (void)mo;
                switch_app(app_id);
                if (ctx_.output != nullptr) {
                    ctx_.output->menu_overlay().close();
                }
            },
        });
    }
    items.push_back(MenuItem {
        "Settings",
        {},
        [this](MenuOverlay &mo) {
            if (ctx_.output != nullptr) {
                mo.push_level(ctx_.output->build_settings_menu());
            }
        },
    });
    items.push_back(MenuItem {
        "Power",
        {},
        [this](MenuOverlay &mo) {
            (void)mo;
            if (ctx_.output != nullptr) {
                ctx_.output->request_shutdown();
            }
        },
    });
    return items;
}

std::vector<MenuItem> AppRegistry::build_inline_menu()
{
    std::vector<MenuItem> items;
    for (const auto &app : apps_) {
        if (app->kind() != AppKind::Inline) {
            continue;
        }
        const std::string app_id = app->id();
        items.push_back(MenuItem {
            app->label(),
            {},
            [this, app_id](MenuOverlay &mo) {
                (void)mo;
                for (const auto &candidate : apps_) {
                    if (candidate->id() == app_id) {
                        candidate->on_enter(ctx_);
                        break;
                    }
                }
            },
        });
    }
    return items;
}

} // namespace braillatron::ui
