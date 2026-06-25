#include "../../documents/contacts_store.h"
#include "../output_hub.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class Phase {
    Search,
    PickMatch,
    Detail,
    Actions,
    AddName,
    AddPhone,
};

enum class ActionKind {
    CopyToDocument,
    EmbossCard,
};

class ContactsApp final : public AppSession {
public:
    std::string id() const override { return "contacts"; }
    std::string label() const override { return "Contacts"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        store_.refresh();
        if (store_.contacts().empty()) {
            announce(ctx, "Contacts ready. No contacts yet. Menu to add a contact or import files.");
            return;
        }
        announce(ctx, "Contacts ready. " + std::to_string(store_.contacts().size()) +
                           " contacts. Type a name and press Enter. Menu to add contact.");
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Contacts closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    std::string composer_line() const override
    {
        if (phase_ == Phase::AddName) {
            return add_name_buffer_;
        }
        if (phase_ == Phase::AddPhone) {
            return add_phone_buffer_;
        }
        return query_buffer_;
    }

    void on_menu_action(const std::string &action, UiContext &ctx) override
    {
        if (action == "add_contact") {
            start_add_contact(ctx);
        }
    }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        if (phase_ == Phase::AddName) {
            add_name_buffer_ += text;
            sync_composer(ctx);
            return;
        }
        if (phase_ == Phase::AddPhone) {
            add_phone_buffer_ += text;
            sync_composer(ctx);
            return;
        }
        if (phase_ != Phase::Search) {
            return;
        }
        query_buffer_ += text;
        sync_composer(ctx);
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        switch (phase_) {
        case Phase::Search:
            handle_search_control(key, ctx);
            break;
        case Phase::PickMatch:
            handle_pick_control(key, ctx);
            break;
        case Phase::Detail:
            handle_detail_control(key, ctx);
            break;
        case Phase::Actions:
            handle_actions_control(key, ctx);
            break;
        case Phase::AddName:
            handle_add_name_control(key, ctx);
            break;
        case Phase::AddPhone:
            handle_add_phone_control(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Search;
        query_buffer_.clear();
        matches_.clear();
        match_index_ = 0;
        action_index_ = 0;
        selected_contact_ = nullptr;
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
    }

    void sync_composer(UiContext &ctx)
    {
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
    }

    void start_add_contact(UiContext &ctx)
    {
        phase_ = Phase::AddName;
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
        announce(ctx, "Add contact. Enter name.");
        sync_composer(ctx);
    }

    void return_to_search(UiContext &ctx)
    {
        phase_ = Phase::Search;
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
        sync_composer(ctx);
        announce(ctx, query_buffer_.empty() ? "Search." : "Search. " + query_buffer_);
    }

    void handle_add_name_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!add_name_buffer_.empty()) {
                add_name_buffer_.pop_back();
                sync_composer(ctx);
                return;
            }
            return_to_search(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (add_name_buffer_.empty()) {
            announce(ctx, "Enter a name first");
            return;
        }

        phase_ = Phase::AddPhone;
        add_phone_buffer_.clear();
        announce(ctx, "Enter phone number.");
        sync_composer(ctx);
    }

    void handle_add_phone_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!add_phone_buffer_.empty()) {
                add_phone_buffer_.pop_back();
                sync_composer(ctx);
                return;
            }
            phase_ = Phase::AddName;
            announce(ctx, "Name. " + add_name_buffer_);
            sync_composer(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (!store_.add_contact(add_name_buffer_, add_phone_buffer_)) {
            announce(ctx, "Could not save contact");
            return;
        }

        announce(ctx, "Contact saved");
        return_to_search(ctx);
    }

    void handle_search_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                sync_composer(ctx);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        store_.refresh();
        matches_ = store_.search(query_buffer_);
        if (matches_.empty()) {
            announce(ctx, query_buffer_.empty() ? "No contacts yet. Menu to add a contact."
                                                : "No matches for " + query_buffer_);
            return;
        }
        if (matches_.size() == 1) {
            open_contact(ctx, matches_.front());
            return;
        }

        match_index_ = 0;
        phase_ = Phase::PickMatch;
        announce_match(ctx);
    }

    void handle_pick_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            matches_.clear();
            match_index_ = 0;
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && match_index_ > 0) {
            --match_index_;
            announce_match(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && match_index_ + 1 < matches_.size()) {
            ++match_index_;
            announce_match(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || matches_.empty()) {
            return;
        }
        open_contact(ctx, matches_[match_index_]);
    }

    void handle_detail_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            selected_contact_ = nullptr;
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        action_index_ = 0;
        phase_ = Phase::Actions;
        announce_action(ctx);
    }

    void handle_actions_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Detail;
            announce_contact(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && action_index_ > 0) {
            --action_index_;
            announce_action(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            action_index_ + 1 < static_cast<size_t>(ActionKind::EmbossCard) + 1) {
            ++action_index_;
            announce_action(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || selected_contact_ == nullptr) {
            return;
        }

        const ActionKind action = static_cast<ActionKind>(action_index_);
        if (action == ActionKind::CopyToDocument) {
            copy_to_document(ctx);
            return;
        }
        if (action == ActionKind::EmbossCard) {
            emboss_card(ctx);
        }
    }

    void open_contact(UiContext &ctx, const documents::Contact &contact)
    {
        selected_contact_ = store_.find_by_id(contact.id);
        if (selected_contact_ == nullptr) {
            selected_contact_ = &contact;
        }
        phase_ = Phase::Detail;
        announce_contact(ctx);
    }

    void announce_match(UiContext &ctx)
    {
        announce(ctx, "Match " + std::to_string(match_index_ + 1) + " of " +
                           std::to_string(matches_.size()) + ". " + matches_[match_index_].name);
    }

    void announce_contact(UiContext &ctx)
    {
        if (selected_contact_ == nullptr) {
            return;
        }
        std::string message = selected_contact_->name;
        if (!selected_contact_->organization.empty()) {
            message += ", " + selected_contact_->organization;
        }
        if (!selected_contact_->phones.empty()) {
            message += ". Phone " + selected_contact_->phones.front();
        }
        if (!selected_contact_->emails.empty()) {
            message += ". Email " + selected_contact_->emails.front();
        }
        if (!selected_contact_->notes.empty()) {
            message += ". " + selected_contact_->notes;
        }
        message += ". Press Enter for actions.";
        announce(ctx, message);
    }

    void announce_action(UiContext &ctx)
    {
        const ActionKind action = static_cast<ActionKind>(action_index_);
        if (action == ActionKind::CopyToDocument) {
            announce(ctx, "Action: Copy to document");
            return;
        }
        if (!config_.emboss_enabled) {
            announce(ctx, "Action: Emboss card. Embossing disabled in settings.");
            return;
        }
        announce(ctx, "Action: Emboss card");
    }

    void copy_to_document(UiContext &ctx)
    {
        if (selected_contact_ == nullptr || ctx.brf == nullptr) {
            announce(ctx, "Document not available");
            return;
        }
        const std::string card = store_.format_card(*selected_contact_);
        for (const std::string &line : split_lines(card)) {
            ctx.brf->append_line(line);
        }
        ctx.brf->save();
        announce(ctx, "Copied contact to document");
        phase_ = Phase::Detail;
    }

    void emboss_card(UiContext &ctx)
    {
        if (selected_contact_ == nullptr) {
            return;
        }
        if (!config_.emboss_enabled || ctx.motion == nullptr || ctx.braille == nullptr) {
            announce(ctx, "Embossing not available");
            return;
        }
        ctx.motion->emboss_text(store_.format_card(*selected_contact_), *ctx.braille);
        announce(ctx, "Embossing contact card");
        phase_ = Phase::Detail;
    }

    static std::vector<std::string> split_lines(const std::string &text)
    {
        std::vector<std::string> lines;
        std::string current;
        for (char ch : text) {
            if (ch == '\n') {
                lines.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        return lines;
    }

    documents::ContactsConfig config_ =
        documents::load_contacts_config("/etc/braillatron/contacts.conf");
    documents::ContactsStore store_ {config_};
    Phase phase_ = Phase::Search;
    std::string query_buffer_;
    std::string add_name_buffer_;
    std::string add_phone_buffer_;
    std::vector<documents::Contact> matches_;
    size_t match_index_ = 0;
    const documents::Contact *selected_contact_ = nullptr;
    size_t action_index_ = 0;
};

} // namespace

std::unique_ptr<AppSession> make_contacts_app()
{
    return std::make_unique<ContactsApp>();
}

} // namespace braillatron::ui
