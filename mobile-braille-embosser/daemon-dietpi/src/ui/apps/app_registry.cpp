#include "app_registry.h"

#include "all_apps.h"
#include "app_util.h"
#include "../output_hub.h"

#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"

#include <algorithm>

namespace braillatron::ui {

namespace {

std::vector<const AppSession *> standalone_apps_sorted(
    const std::vector<std::unique_ptr<AppSession>> &apps)
{
    std::vector<const AppSession *> standalone;
    for (const auto &app : apps) {
        if (app->kind() == AppKind::Standalone && app->show_in_launcher()) {
            standalone.push_back(app.get());
        }
    }
    std::sort(standalone.begin(), standalone.end(),
        [](const AppSession *a, const AppSession *b) { return a->label() < b->label(); });
    return standalone;
}

void insert_media_playback_menu_items(std::vector<MenuItem> &items, UiContext &ctx, size_t index)
{
    if (ctx.output == nullptr || !ctx.output->media_playing()) {
        return;
    }

    auto insert_at = [&](size_t at, MenuItem item) {
        const size_t clamped = std::min(at, items.size());
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(clamped), std::move(item));
    };

    const std::string pause_label =
        ctx.output->media_paused() ? "Resume playback" : "Pause playback";
    insert_at(index,
              MenuItem {
                  pause_label,
                  {},
                  [&ctx](MenuOverlay &mo) {
                      if (ctx.connect == nullptr || ctx.output == nullptr) {
                          return;
                      }
                      const std::string response = ctx.connect->request("media.pause");
                      if (braillatron::connect::json_get_bool(response, "ok", false)) {
                          const bool paused =
                              braillatron::connect::json_get_bool(response, "paused", false);
                          ctx.output->set_media_paused(paused);
                          ctx.output->announce_message(paused ? "Playback paused"
                                                              : "Playback resumed");
                      }
                      mo.close();
                      ctx.output->sync_chrome(false);
                  },
              });
    insert_at(index + 1,
              MenuItem {
                  "Skip back 30 seconds",
                  {},
                  [&ctx](MenuOverlay &mo) {
                      if (ctx.connect != nullptr) {
                          ctx.connect->request("media.skip_backward");
                          if (ctx.output != nullptr) {
                              ctx.output->announce_message("Skipped back");
                          }
                      }
                      mo.close();
                      if (ctx.output != nullptr) {
                          ctx.output->sync_chrome(false);
                      }
                  },
              });
    insert_at(index + 2,
              MenuItem {
                  "Skip forward 30 seconds",
                  {},
                  [&ctx](MenuOverlay &mo) {
                      if (ctx.connect != nullptr) {
                          ctx.connect->request("media.skip_forward");
                          if (ctx.output != nullptr) {
                              ctx.output->announce_message("Skipped forward");
                          }
                      }
                      mo.close();
                      if (ctx.output != nullptr) {
                          ctx.output->sync_chrome(false);
                      }
                  },
              });
    insert_at(index + 3,
              MenuItem {
                  "Stop playback",
                  {},
                  [&ctx](MenuOverlay &mo) {
                      if (ctx.connect != nullptr) {
                          ctx.connect->request("media.stop");
                      }
                      if (ctx.output != nullptr) {
                          ctx.output->set_media_playing(false);
                          ctx.output->announce_message("Playback stopped");
                      }
                      mo.close();
                      if (ctx.output != nullptr) {
                          ctx.output->sync_chrome(false);
                      }
                  },
              });
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
    register_app(make_factory_test_app());
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
            clear_word_buffer();
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
        clear_word_buffer();
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

void AppRegistry::poll(uint64_t now_ms)
{
    tick_watchdog(now_ms);
    if (active_inline_ != nullptr) {
        active_inline_->on_poll(ctx_);
    }
    if (active_ != nullptr) {
        active_->on_poll(ctx_);
    }
}

void AppRegistry::mark_busy(uint64_t now_ms)
{
    if (busy_since_ms_ == 0) {
        busy_since_ms_ = now_ms;
    }
}

void AppRegistry::clear_busy()
{
    busy_since_ms_ = 0;
}

void AppRegistry::tick_watchdog(uint64_t now_ms)
{
    if (frozen_prompt_open_) {
        return;
    }
    if (active_ == nullptr) {
        clear_busy();
        return;
    }
    if (busy_since_ms_ == 0) {
        return;
    }
    if (now_ms - busy_since_ms_ < kFrozenTimeoutMs) {
        return;
    }
    open_frozen_prompt();
}

void AppRegistry::open_frozen_prompt()
{
    if (frozen_prompt_open_ || ctx_.output == nullptr) {
        return;
    }
    frozen_prompt_open_ = true;
    ctx_.output->open_frozen_app_confirm(this);
}

void AppRegistry::dismiss_frozen_prompt()
{
    frozen_prompt_open_ = false;
    clear_busy();
}

void AppRegistry::on_global_menu(bool open)
{
    if (!open && frozen_prompt_open_) {
        dismiss_frozen_prompt();
    }
    if (ctx_.output == nullptr) {
        return;
    }
    ctx_.output->on_menu_overlay(open);
}

void AppRegistry::on_chord(uint8_t dot_mask)
{
    if (word_buffer_active()) {
        if (dot_mask != 0) {
            word_buffer_.push_chord(dot_mask, ctx_.braille_input);
            echo_typed_chord(dot_mask);
            if (ctx_.output != nullptr) {
                ctx_.output->sync_chrome(false);
            }
        }
        return;
    }

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
    if (word_buffer_active()) {
        for (char ch : text) {
            if (ch == ' ') {
                const std::string word =
                    word_buffer_.commit_word(ctx_.braille_input, word_buffer_uncontracted());
                if (!word.empty()) {
                    echo_typed_commit(word);
                    deliver_text(focused_app(), word);
                }
                echo_typed_text(" ");
                deliver_text(focused_app(), " ");
            } else {
                const std::string word =
                    word_buffer_.commit_word(ctx_.braille_input, word_buffer_uncontracted());
                if (!word.empty()) {
                    echo_typed_commit(word);
                    deliver_text(focused_app(), word);
                }
                echo_typed_text(std::string(1, ch));
                deliver_text(focused_app(), std::string(1, ch));
            }
        }
        if (ctx_.output != nullptr) {
            ctx_.output->sync_chrome(false);
        }
        return;
    }

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
    if (pressed && key == keyboard::ControlKey::Backspace && word_buffer_active()) {
        const std::string before = word_buffer_.preview();
        if (word_buffer_.pop_chord(ctx_.braille_input)) {
            echo_typed_deleted(before);
            if (ctx_.output != nullptr) {
                ctx_.output->sync_chrome(false);
            }
            return;
        }
    }

    if (pressed && key == keyboard::ControlKey::Enter && word_buffer_active()) {
        const std::string word =
            word_buffer_.commit_word(ctx_.braille_input, word_buffer_uncontracted());
        if (!word.empty()) {
            echo_typed_commit(word);
            deliver_text(focused_app(), word);
        }
        if (ctx_.output != nullptr) {
            ctx_.output->sync_chrome(false);
        }
    }

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
            if (ctx_.output != nullptr) {
                ctx_.output->push_power_confirm(mo);
            }
        },
    });
    insert_media_playback_menu_items(items, ctx_, 0);
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

    if (active_ != nullptr) {
        items.insert(items.begin(),
                     MenuItem {
                         "Print",
                         {},
                         [this](MenuOverlay &mo) {
                             (void)mo;
                             if (active_ != nullptr) {
                                 active_->on_menu_action("print", ctx_);
                             }
                             if (ctx_.output != nullptr) {
                                 ctx_.output->menu_overlay().close();
                                 ctx_.output->sync_chrome(false);
                             }
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

    if (active_ != nullptr && active_->menu_has_remove()) {
        size_t remove_index = active_ != nullptr ? 1 : 0;
        if (active_ != nullptr && active_->id() == "contacts") {
            ++remove_index;
        }
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(remove_index),
                     MenuItem {
                         "Remove item",
                         {},
                         [this](MenuOverlay &mo) {
                             (void)mo;
                             if (active_ != nullptr) {
                                 active_->on_menu_action("remove", ctx_);
                             }
                             if (ctx_.output != nullptr) {
                                 ctx_.output->menu_overlay().close();
                                 ctx_.output->sync_chrome(false);
                             }
                         },
                     });
    }

    if (active_ != nullptr && active_->menu_has_rename()) {
        size_t rename_index = active_ != nullptr ? 1 : 0;
        if (active_ != nullptr && active_->id() == "contacts") {
            ++rename_index;
        }
        if (active_ != nullptr && active_->menu_has_remove()) {
            ++rename_index;
        }
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(rename_index),
                     MenuItem {
                         "Rename item",
                         {},
                         [this](MenuOverlay &mo) {
                             (void)mo;
                             if (active_ != nullptr) {
                                 active_->on_menu_action("rename", ctx_);
                             }
                             if (ctx_.output != nullptr) {
                                 ctx_.output->menu_overlay().close();
                                 ctx_.output->sync_chrome(false);
                             }
                         },
                     });
    }

    if (active_ != nullptr && active_->menu_has_import_usb()) {
        size_t import_index = active_ != nullptr ? 1 : 0;
        if (active_ != nullptr && active_->id() == "contacts") {
            ++import_index;
        }
        if (active_ != nullptr && active_->menu_has_remove()) {
            ++import_index;
        }
        if (active_ != nullptr && active_->menu_has_rename()) {
            ++import_index;
        }
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(import_index),
                     MenuItem {
                         "Import from USB",
                         {},
                         [this](MenuOverlay &mo) {
                             (void)mo;
                             if (active_ != nullptr) {
                                 active_->on_menu_action("import_usb", ctx_);
                             }
                             if (ctx_.output != nullptr) {
                                 ctx_.output->menu_overlay().close();
                                 ctx_.output->sync_chrome(false);
                             }
                         },
                     });
    }

    size_t media_index = 0;
    if (active_ != nullptr) {
        ++media_index;
    }
    if (active_ != nullptr && active_->id() == "contacts") {
        ++media_index;
    }
    if (active_ != nullptr && active_->menu_has_remove()) {
        ++media_index;
    }
    if (active_ != nullptr && active_->menu_has_rename()) {
        ++media_index;
    }
    if (active_ != nullptr && active_->menu_has_import_usb()) {
        ++media_index;
    }
    insert_media_playback_menu_items(items, ctx_, media_index);

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

AppSession *AppRegistry::focused_app() const
{
    if (active_inline_ != nullptr) {
        return active_inline_;
    }
    return active_;
}

bool AppRegistry::word_buffer_active() const
{
    AppSession *app = focused_app();
    return app != nullptr && app->buffers_braille_words();
}

bool AppRegistry::word_buffer_uncontracted() const
{
    AppSession *app = focused_app();
    return app != nullptr && app->buffers_uncontracted_braille_words();
}

void AppRegistry::clear_word_buffer()
{
    word_buffer_.clear();
}

void AppRegistry::deliver_text(AppSession *app, const std::string &text)
{
    if (app == nullptr || text.empty()) {
        return;
    }
    app->on_text(text, ctx_);
}

void AppRegistry::echo_typed_chord(uint8_t dot_mask)
{
    AppSession *app = focused_app();
    if (app != nullptr && app->masks_typing_echo()) {
        announce_typing(ctx_, "star");
        return;
    }

    std::string glyph;
    if (ctx_.braille_input != nullptr) {
        if (const auto cell = ctx_.braille_input->translate_backward_dot_uncontracted(dot_mask)) {
            glyph = *cell;
        }
    }
    if (glyph.empty()) {
        announce_typing(ctx_, "unknown");
        return;
    }
    announce_typing(ctx_, spoken_typed_text(glyph));
}

void AppRegistry::echo_typed_text(const std::string &text)
{
    if (text.empty()) {
        return;
    }
    AppSession *app = focused_app();
    if (app != nullptr && app->masks_typing_echo()) {
        announce_typing(ctx_, "star");
        return;
    }
    announce_typing(ctx_, spoken_typed_text(text));
}

void AppRegistry::echo_typed_commit(const std::string &word)
{
    if (word.empty()) {
        return;
    }
    AppSession *app = focused_app();
    if (app != nullptr && app->masks_typing_echo()) {
        return;
    }
    announce_typing(ctx_, word);
}

void AppRegistry::echo_typed_deleted(const std::string &preview_before)
{
    AppSession *app = focused_app();
    if (app != nullptr && app->masks_typing_echo()) {
        announce_typing(ctx_, "deleted");
        return;
    }

    const std::string &after = word_buffer_.preview();
    if (preview_before.size() > after.size()) {
        announce_typing(ctx_,
                        "deleted " + spoken_typed_text(preview_before.substr(after.size())));
        return;
    }
    announce_typing(ctx_, "deleted");
}

bool AppRegistry::defers_chord_text() const
{
    AppSession *app = focused_app();
    if (app == nullptr) {
        return false;
    }
    if (app->id() == "brailler") {
        return true;
    }
    return app->buffers_braille_words();
}

std::string AppRegistry::chord_preview() const
{
    if (!word_buffer_active() || !word_buffer_.has_pending()) {
        return {};
    }
    return word_buffer_.preview();
}

} // namespace braillatron::ui
