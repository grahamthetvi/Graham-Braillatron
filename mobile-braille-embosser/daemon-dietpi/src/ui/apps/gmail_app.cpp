#include "../../connect/connect_client.h"
#include "../../connect/connect_config.h"
#include "../../connect/gmail_backend.h"
#include "../../connect/json_utils.h"
#include "../../documents/brf_store.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class Phase { Menu, Inbox, Read, ActionSelect, Compose, Reply };

enum class ActionKind {
    ExportBrf,
    Archive,
    Star,
    Delete,
};

constexpr int kActionCount = 4;

struct InboxItem {
    std::string id;
    std::string from;
    std::string subject;
    std::string snippet;
};

class GmailApp final : public AppSession {
public:
    std::string id() const override { return "gmail"; }
    std::string label() const override { return "Gmail"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }
        phase_ = Phase::Menu;
        announce(ctx, "Gmail. Inbox or Compose. Press Enter.");
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Gmail closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
    }

    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override
    {
        return phase_ == Phase::Compose || phase_ == Phase::Reply;
    }

    void on_text(const std::string &text, UiContext &) override
    {
        if (phase_ == Phase::Compose || phase_ == Phase::Reply) {
            compose_ += text;
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || ctx.connect == nullptr) {
            return;
        }

        switch (phase_) {
        case Phase::Menu:
            handle_menu(key, ctx);
            break;
        case Phase::Inbox:
            handle_inbox(key, ctx);
            break;
        case Phase::Read:
            handle_read(key, ctx);
            break;
        case Phase::ActionSelect:
            handle_action_select(key, ctx);
            break;
        case Phase::Compose:
            handle_compose(key, ctx);
            break;
        case Phase::Reply:
            handle_reply(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Menu;
        menu_index_ = 0;
        inbox_.clear();
        inbox_index_ = 0;
        action_index_ = 0;
        compose_.clear();
        compose_to_.clear();
        compose_subject_.clear();
        compose_field_ = 0;
        active_message_.clear();
        active_from_.clear();
        active_subject_.clear();
        active_body_.clear();
        active_id_.clear();
        pending_announce_.clear();
    }

    void handle_menu(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::DpadUp && menu_index_ > 0) {
            --menu_index_;
            announce(ctx, menu_label());
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && menu_index_ + 1 < 2) {
            ++menu_index_;
            announce(ctx, menu_label());
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            if (menu_index_ == 0) {
                load_inbox(ctx);
            } else {
                start_compose(ctx);
            }
        }
    }

    void handle_inbox(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Menu;
            announce(ctx, "Gmail menu");
            return;
        }
        if (inbox_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && inbox_index_ > 0) {
            --inbox_index_;
            announce(ctx, inbox_[inbox_index_].subject);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && inbox_index_ + 1 < inbox_.size()) {
            ++inbox_index_;
            announce(ctx, inbox_[inbox_index_].subject);
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            open_message(ctx);
        }
    }

    void handle_read(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Inbox;
            announce(ctx, inbox_.empty() ? "Inbox empty" : inbox_[inbox_index_].subject);
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            phase_ = Phase::Reply;
            compose_.clear();
            announce(ctx, "Reply to " + active_from_);
            return;
        }
        if (key == keyboard::ControlKey::Menu) {
            phase_ = Phase::ActionSelect;
            action_index_ = 0;
            announce(ctx, action_label());
        }
    }

    void handle_action_select(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Read;
            announce(ctx, active_subject_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && action_index_ > 0) {
            --action_index_;
            announce(ctx, action_label());
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && action_index_ + 1 < kActionCount) {
            ++action_index_;
            announce(ctx, action_label());
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            run_action(ctx);
        }
    }

    void handle_compose(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!compose_.empty()) {
                compose_.pop_back();
                return;
            }
            if (compose_field_ > 0) {
                --compose_field_;
                compose_ = compose_field_ == 0 ? compose_to_ : compose_subject_;
                announce(ctx, compose_field_ == 0 ? "To field" : "Subject field");
                return;
            }
            phase_ = Phase::Menu;
            announce(ctx, "Gmail menu");
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            if (compose_field_ == 0) {
                compose_to_ = compose_;
                compose_.clear();
                compose_field_ = 1;
                announce(ctx, "Subject field");
                return;
            }
            if (compose_field_ == 1) {
                compose_subject_ = compose_;
                compose_.clear();
                compose_field_ = 2;
                announce(ctx, "Message body");
                return;
            }
            send_compose(ctx);
        }
    }

    void handle_reply(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!compose_.empty()) {
                compose_.pop_back();
                return;
            }
            phase_ = Phase::Read;
            announce(ctx, active_subject_);
            return;
        }
        if (key == keyboard::ControlKey::Enter && !compose_.empty()) {
            send_reply(ctx);
        }
    }

    std::string menu_label() const
    {
        return menu_index_ == 0 ? "Inbox" : "Compose";
    }

    std::string action_label() const
    {
        switch (static_cast<ActionKind>(action_index_)) {
        case ActionKind::ExportBrf:
            return "Export BRF";
        case ActionKind::Archive:
            return "Archive";
        case ActionKind::Star:
            return "Star";
        case ActionKind::Delete:
            return "Delete";
        }
        return "Export BRF";
    }

    void load_inbox(UiContext &ctx)
    {
        phase_ = Phase::Inbox;
        inbox_.clear();
        inbox_index_ = 0;
        ctx.connect->request_async("gmail.list_inbox", "", [this](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                pending_announce_ = "Gmail not linked. Use Settings Accounts to link Gmail.";
                phase_ = Phase::Menu;
                return;
            }
            const std::string array = braillatron::connect::json_get_array_body(response, "messages");
            for (const auto &obj : braillatron::connect::json_split_objects("[" + array + "]")) {
                InboxItem item;
                item.id = braillatron::connect::json_get_string(obj, "id");
                item.from = braillatron::connect::json_get_string(obj, "from");
                item.subject = braillatron::connect::json_get_string(obj, "subject");
                item.snippet = braillatron::connect::json_get_string(obj, "snippet");
                if (!item.id.empty()) {
                    inbox_.push_back(std::move(item));
                }
            }
            if (inbox_.empty()) {
                pending_announce_ = "Inbox empty";
            } else {
                pending_announce_ = inbox_.front().subject;
            }
        });
    }

    void open_message(UiContext &ctx)
    {
        const InboxItem item = inbox_[inbox_index_];
        ctx.connect->request_async(
            "gmail.read_message",
            "\"message_id\":\"" + braillatron::connect::json_escape(item.id) + "\"",
            [this, item](const std::string &response) {
                if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                    pending_announce_ = "Could not read message";
                    return;
                }
                const size_t msg_pos = response.find("\"message\":{");
                if (msg_pos == std::string::npos) {
                    pending_announce_ = "Message unavailable";
                    return;
                }
                const std::string block = response.substr(msg_pos + 10);
                active_id_ = braillatron::connect::json_get_string(block, "id");
                active_from_ = braillatron::connect::json_get_string(block, "from");
                active_subject_ = braillatron::connect::json_get_string(block, "subject");
                active_body_ = braillatron::connect::json_get_string(block, "body");
                phase_ = Phase::Read;
                pending_announce_ = "From " + active_from_ + ". " + active_subject_ + ". " +
                                   active_body_ + ". Enter to reply. Menu for export or actions.";
            });
    }

    void start_compose(UiContext &ctx)
    {
        (void)ctx;
        phase_ = Phase::Compose;
        compose_field_ = 0;
        compose_.clear();
        compose_to_.clear();
        compose_subject_.clear();
        announce(ctx, "Compose. To field");
    }

    void send_compose(UiContext &ctx)
    {
        const std::string to = compose_to_;
        const std::string subject = compose_subject_;
        const std::string body = compose_;
        ctx.connect->request_async(
            "gmail.send",
            "\"to\":\"" + braillatron::connect::json_escape(to) + "\",\"subject\":\"" +
                braillatron::connect::json_escape(subject) + "\",\"body\":\"" +
                braillatron::connect::json_escape(body) + "\"",
            [this](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    compose_.clear();
                    phase_ = Phase::Menu;
                    pending_announce_ = "Message sent";
                } else {
                    pending_announce_ = "Send failed";
                }
            });
    }

    void send_reply(UiContext &ctx)
    {
        const std::string body = compose_;
        const std::string id = active_id_;
        ctx.connect->request_async(
            "gmail.reply",
            "\"message_id\":\"" + braillatron::connect::json_escape(id) + "\",\"body\":\"" +
                braillatron::connect::json_escape(body) + "\"",
            [this](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    compose_.clear();
                    phase_ = Phase::Inbox;
                    pending_announce_ = "Reply sent";
                } else {
                    pending_announce_ = "Reply failed";
                }
            });
    }

    void export_brf(UiContext &ctx)
    {
        (void)ctx;
        const std::string path = config_.export_dir + "/" +
                                 braillatron::connect::GmailBackend::export_filename(active_subject_);
        documents::BrfStore store(path);
        store.set_lines(braillatron::connect::GmailBackend::format_message_brf_lines(
            active_from_, active_subject_, active_body_));
        if (!store.save()) {
            pending_announce_ = "BRF export failed";
            phase_ = Phase::Read;
            return;
        }
        phase_ = Phase::Read;
        pending_announce_ = "Exported email to BRF";
    }

    void run_action(UiContext &ctx)
    {
        const ActionKind action = static_cast<ActionKind>(action_index_);
        if (action == ActionKind::ExportBrf) {
            export_brf(ctx);
            return;
        }

        const std::string id = active_id_;
        std::string cmd;
        if (action == ActionKind::Archive) {
            cmd = "gmail.archive";
        } else if (action == ActionKind::Star) {
            cmd = "gmail.star";
        } else {
            cmd = "gmail.delete";
        }
        ctx.connect->request_async(
            cmd, "\"message_id\":\"" + braillatron::connect::json_escape(id) + "\"",
            [this, action](const std::string &response) {
                if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                    pending_announce_ = "Action failed";
                    phase_ = Phase::Read;
                    return;
                }
                if (action == ActionKind::Archive || action == ActionKind::Delete) {
                    if (inbox_index_ < inbox_.size()) {
                        inbox_.erase(inbox_.begin() + static_cast<long>(inbox_index_));
                        if (inbox_index_ >= inbox_.size() && !inbox_.empty()) {
                            inbox_index_ = inbox_.size() - 1;
                        }
                    }
                    phase_ = Phase::Inbox;
                    pending_announce_ =
                        action == ActionKind::Archive ? "Archived" : "Deleted";
                    if (!inbox_.empty()) {
                        pending_announce_ += ". " + inbox_[inbox_index_].subject;
                    }
                } else {
                    phase_ = Phase::Read;
                    pending_announce_ = "Starred";
                }
            });
    }

    braillatron::connect::GmailConfig config_ =
        braillatron::connect::load_gmail_config("/etc/braillatron/gmail.conf");

    Phase phase_ = Phase::Menu;
    size_t menu_index_ = 0;
    std::vector<InboxItem> inbox_;
    size_t inbox_index_ = 0;
    size_t action_index_ = 0;
    int compose_field_ = 0;
    std::string compose_;
    std::string compose_to_;
    std::string compose_subject_;
    std::string active_id_;
    std::string active_from_;
    std::string active_subject_;
    std::string active_body_;
    std::string active_message_;
    std::string pending_announce_;
};

} // namespace

std::unique_ptr<AppSession> make_gmail_app()
{
    return std::make_unique<GmailApp>();
}

} // namespace braillatron::ui
