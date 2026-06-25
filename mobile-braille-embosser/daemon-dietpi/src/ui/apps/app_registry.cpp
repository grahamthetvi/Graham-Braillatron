#include "app_registry.h"

#include "all_apps.h"
#include "../output_hub.h"

#include "../../connect/connect_client.h"

#include <algorithm>

namespace braillatron::ui {

namespace {

std::vector<const AppSession *> standalone_apps_sorted(
    const std::vector<std::unique_ptr<AppSession>> &apps)
{
    std::vector<const AppSession *> standalone;
    for (const auto &app : apps) {
        if (app->kind() == AppKind::Standalone) {
            standalone.push_back(app.get());
        }
    }
    std::sort(standalone.begin(), standalone.end(),
        [](const AppSession *a, const AppSession *b) { return a->label() < b->label(); });
    return standalone;
}

} // namespace

AppRegistry::AppRegistry()
{
    register_app(make_brailler_app());
    register_app(make_calculator_app());
    register_app(make_transcriber_app());
    register_app(make_morse_learn_app());
    register_app(make_network_app());
    register_app(make_bluetooth_setup_app());
    register_app(make_youtube_app());
    register_app(make_messages_app());
    register_app(make_library_app());
    register_app(make_localsend_app());
    register_app(make_wikipedia_app());
    register_app(make_dictionary_app());
    register_app(make_spelling_app());
    register_app(make_contacts_app());
    register_app(make_music_app());
    register_app(make_weather_app());
    register_app(make_podcasts_app());
    register_app(make_radio_app());
    register_app(make_gmail_app());
    register_app(make_quick_status_inline());
    register_app(make_timer_inline());
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
            if (active_inline_ != nullptr) {
                active_inline_->on_exit(ctx_);
                active_inline_ = nullptr;
            }
            if (active_ != nullptr) {
                active_->on_exit(ctx_);
            }
            active_ = app.get();
            active_->on_enter(ctx_);
            if (ctx_.output != nullptr) {
                ctx_.output->sync_chrome(false);
            }
            return true;
        }
    }
    return false;
}

bool AppRegistry::enter_inline(const std::string &id)
{
    if (active_inline_ != nullptr) {
        active_inline_->on_exit(ctx_);
        active_inline_ = nullptr;
    }
    for (const auto &app : apps_) {
        if (app->id() == id && app->kind() == AppKind::Inline) {
            active_inline_ = app.get();
            active_inline_->on_enter(ctx_);
            if (ctx_.output != nullptr) {
                ctx_.output->sync_chrome(false);
            }
            return true;
        }
    }
    return false;
}

void AppRegistry::exit_inline()
{
    if (active_inline_ != nullptr) {
        active_inline_->on_exit(ctx_);
        active_inline_ = nullptr;
        if (ctx_.output != nullptr) {
            ctx_.output->sync_chrome(false);
        }
    }
}

void AppRegistry::exit()
{
    if (active_ != nullptr) {
        active_->on_exit(ctx_);
        active_ = nullptr;
        if (ctx_.output != nullptr) {
            ctx_.output->sync_chrome(false);
        }
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
    if (active_inline_ != nullptr) {
        active_inline_->on_poll(ctx_);
    }
    if (active_ != nullptr) {
        active_->on_poll(ctx_);
    }
}

void AppRegistry::on_chord(uint8_t dot_mask)
{
    if (active_inline_ != nullptr) {
        active_inline_->on_chord(dot_mask, ctx_);
        return;
    }
    if (active_ != nullptr) {
        active_->on_chord(dot_mask, ctx_);
    }
}

void AppRegistry::on_text(const std::string &text)
{
    if (active_inline_ != nullptr) {
        active_inline_->on_text(text, ctx_);
        return;
    }
    if (active_ != nullptr) {
        active_->on_text(text, ctx_);
    }
}

void AppRegistry::on_control(keyboard::ControlKey key, bool pressed)
{
    if (active_inline_ != nullptr) {
        active_inline_->on_control(key, pressed, ctx_);
        return;
    }
    if (active_ != nullptr) {
        active_->on_control(key, pressed, ctx_);
    }
}

void AppRegistry::on_connect_event(const braillatron::connect::ConnectEvent &event)
{
    if (active_inline_ != nullptr) {
        active_inline_->on_connect_event(event, ctx_);
    }
    if (active_ != nullptr) {
        active_->on_connect_event(event, ctx_);
    }
}

std::vector<MenuItem> AppRegistry::build_launcher_menu()
{
    std::vector<MenuItem> items;
    for (const AppSession *app : standalone_apps_sorted(apps_)) {
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

std::vector<std::string> AppRegistry::launcher_labels() const
{
    std::vector<std::string> labels;
    for (const AppSession *app : standalone_apps_sorted(apps_)) {
        labels.push_back(app->label());
    }
    labels.push_back("Settings");
    labels.push_back("Power");
    return labels;
}

std::string AppRegistry::launcher_id_for_label(const std::string &label) const
{
    for (const auto &app : apps_) {
        if (app->kind() == AppKind::Standalone && app->label() == label) {
            return app->id();
        }
    }
    return "";
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
                enter_inline(app_id);
            },
        });
    }

    if (active_ != nullptr && active_->id() == "contacts") {
        items.insert(items.begin(),
                     MenuItem {
                         "Add contact",
                         {},
                         [this](MenuOverlay &mo) {
                             (void)mo;
                             if (active_ != nullptr) {
                                 active_->on_menu_action("add_contact", ctx_);
                             }
                             if (ctx_.output != nullptr) {
                                 ctx_.output->menu_overlay().close();
                                 ctx_.output->sync_chrome(false);
                             }
                         },
                     });
    }

    if (active_ != nullptr && active_->id() == "brailler") {
        items.push_back(MenuItem {
            "Look up word",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (ctx_.output != nullptr) {
                    ctx_.output->announce_message(
                        "Word lookup from Document is not available yet. Use the Dictionary app.");
                }
            },
        });
    }

    items.push_back(MenuItem {
        "Back to app list",
        {},
        [this](MenuOverlay &mo) {
            (void)mo;
            exit_inline();
            exit();
            if (ctx_.output != nullptr) {
                ctx_.output->menu_overlay().close();
                ctx_.output->announce_message("Back to app list");
                ctx_.output->sync_chrome(false);
            }
        },
    });

    return items;
}

} // namespace braillatron::ui
