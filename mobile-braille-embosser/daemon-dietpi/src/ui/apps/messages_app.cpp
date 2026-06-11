#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "app_session.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../keyboard/chord_engine.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

void announce(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_message(msg);
    }
}

enum class MessagesState { ChatList, Thread, Compose };

struct ChatItem {
    std::string id;
    std::string name;
};

class MessagesApp final : public AppSession {
public:
    std::string id() const override { return "messages"; }
    std::string label() const override { return "Messages"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        state_ = MessagesState::ChatList;
        compose_.clear();
        load_chats(ctx);
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Messages closed"); }

    void on_poll(UiContext &ctx) override
    {
        if (ctx.connect == nullptr) {
            return;
        }
        ctx.connect->poll_events([this, &ctx](const braillatron::connect::ConnectEvent &event) {
            if (event.type != "message.received") {
                return;
            }
            const std::string from = braillatron::connect::json_get_string(event.data_json, "from");
            const std::string text = braillatron::connect::json_get_string(event.data_json, "text");
            if (ctx.output != nullptr) {
                ctx.output->play_boundary_haptic();
                ctx.output->announce_message("Message from " + from + ". " + text);
            }
        });
    }

    void on_chord(uint8_t, UiContext &ctx) override
    {
        if (state_ == MessagesState::ChatList && selected_chat_ + 1 < chats_.size()) {
            ++selected_chat_;
            announce(ctx, chats_[selected_chat_].name);
        }
    }

    void on_text(const std::string &text, UiContext &) override
    {
        if (state_ == MessagesState::Compose) {
            compose_ += text;
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || ctx.connect == nullptr) {
            return;
        }

        if (key == keyboard::ControlKey::Enter) {
            if (state_ == MessagesState::ChatList && !chats_.empty()) {
                open_thread(ctx);
            } else if (state_ == MessagesState::Thread) {
                state_ = MessagesState::Compose;
                compose_.clear();
                announce(ctx, "Compose message");
            } else if (state_ == MessagesState::Compose && !compose_.empty()) {
                send_message(ctx);
            }
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (state_ == MessagesState::Compose && !compose_.empty()) {
                compose_.pop_back();
            } else if (state_ == MessagesState::Thread) {
                state_ = MessagesState::ChatList;
                announce(ctx, "Chat list");
            } else if (state_ == MessagesState::Compose) {
                state_ = MessagesState::Thread;
            }
        }

        if (key == keyboard::ControlKey::DpadUp && state_ == MessagesState::ChatList &&
            selected_chat_ > 0) {
            --selected_chat_;
            announce(ctx, chats_[selected_chat_].name);
        }
    }

private:
    void load_chats(UiContext &ctx)
    {
        chats_.clear();
        selected_chat_ = 0;
        const std::string response = ctx.connect->request("signal.list_chats");
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            announce(ctx, "Signal not linked. Use Settings Accounts to link.");
            return;
        }
        const size_t arr = response.find("\"chats\":[");
        if (arr == std::string::npos) {
            announce(ctx, "No chats");
            return;
        }
        const size_t end = response.find(']', arr);
        const std::string array = response.substr(arr + 8, end - arr - 8);
        for (const auto &obj : braillatron::connect::json_split_objects("[" + array + "]")) {
            ChatItem chat;
            chat.id = braillatron::connect::json_get_string(obj, "id");
            chat.name = braillatron::connect::json_get_string(obj, "name");
            if (!chat.id.empty()) {
                chats_.push_back(std::move(chat));
            }
        }
        if (chats_.empty()) {
            announce(ctx, "No chats found");
        } else {
            announce(ctx, chats_.front().name);
        }
    }

    void open_thread(UiContext &ctx)
    {
        active_chat_ = chats_[selected_chat_];
        state_ = MessagesState::Thread;
        const std::string response = ctx.connect->request(
            "signal.list_messages", "\"recipient\":\"" + active_chat_.id + "\"");
        const size_t arr = response.find("\"messages\":[");
        if (arr == std::string::npos) {
            announce(ctx, "No messages with " + active_chat_.name);
            return;
        }
        const size_t end = response.find(']', arr);
        const std::string array = response.substr(arr + 11, end - arr - 11);
        for (const auto &obj : braillatron::connect::json_split_objects("[" + array + "]")) {
            const std::string from = braillatron::connect::json_get_string(obj, "from");
            const std::string text = braillatron::connect::json_get_string(obj, "text");
            announce(ctx, from + ": " + text);
        }
    }

    void send_message(UiContext &ctx)
    {
        const std::string response = ctx.connect->request(
            "signal.send", "\"recipient\":\"" + braillatron::connect::json_escape(active_chat_.id) +
                               "\",\"text\":\"" + braillatron::connect::json_escape(compose_) + "\"");
        if (braillatron::connect::json_get_bool(response, "ok", false)) {
            announce(ctx, "Message sent");
            compose_.clear();
            state_ = MessagesState::Thread;
        } else {
            announce(ctx, "Send failed");
        }
    }

    MessagesState state_ = MessagesState::ChatList;
    std::vector<ChatItem> chats_;
    ChatItem active_chat_;
    size_t selected_chat_ = 0;
    std::string compose_;
};

} // namespace

std::unique_ptr<AppSession> make_messages_app()
{
    return std::make_unique<MessagesApp>();
}

} // namespace braillatron::ui
