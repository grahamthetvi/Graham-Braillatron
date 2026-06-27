#include "../../documents/contacts_store.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"
#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

constexpr const char *kRecentStatePath = "/data/braillatron/contacts/recent.json";
constexpr size_t kMaxRecentContacts = 3;

enum class Phase {
    MainMenu,
    RecentList,
    AddressBook,
    Search,
    Detail,
    Actions,
    AddName,
    AddPhone,
};

enum class ActionKind {
    CopyToDocument,
    EmbossCard,
};

enum class MainMenuKind {
    RecentlyViewed,
    AddressBook,
    AddNewContact,
};

std::vector<std::string> load_recent_contact_ids()
{
    std::ifstream in(kRecentStatePath);
    if (!in) {
        return {};
    }
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> ids;
    const size_t array_pos = json.find("\"recent\"");
    if (array_pos == std::string::npos) {
        return ids;
    }
    size_t pos = json.find('[', array_pos);
    if (pos == std::string::npos) {
        return ids;
    }
    ++pos;
    while (pos < json.size()) {
        const size_t quote = json.find('"', pos);
        if (quote == std::string::npos || json.find(']', pos) < quote) {
            break;
        }
        const size_t end = json.find('"', quote + 1);
        if (end == std::string::npos) {
            break;
        }
        ids.push_back(json.substr(quote + 1, end - quote - 1));
        pos = end + 1;
        if (ids.size() >= kMaxRecentContacts) {
            break;
        }
    }
    return ids;
}

bool save_recent_contact_ids(const std::vector<std::string> &ids)
{
    std::ofstream out(kRecentStatePath, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "{\n  \"recent\":[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "\n    \"" << ids[i] << '"';
    }
    out << "\n  ]\n}\n";
    return static_cast<bool>(out);
}

class ContactsApp final : public AppSession {
public:
    std::string id() const override { return "contacts"; }
    std::string label() const override { return "Contacts"; }
    AppKind kind() const override { return AppKind::Standalone; }

    bool browse_list_active() const override
    {
        return phase_ == Phase::MainMenu || phase_ == Phase::RecentList || phase_ == Phase::AddressBook ||
               phase_ == Phase::Actions;
    }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string browse_breadcrumb() const override
    {
        switch (phase_) {
        case Phase::RecentList:
            return "Recently Viewed";
        case Phase::AddressBook:
            return "Address Book";
        case Phase::Actions:
            return "Actions";
        default:
            return {};
        }
    }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        store_.refresh();
        load_recent_contacts();
        enter_main_menu(ctx, "Contacts ready. Use up and down to browse. Press Enter to open.");
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Contacts closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override
    {
        return phase_ == Phase::Search || phase_ == Phase::AddName || phase_ == Phase::AddPhone;
    }

    std::string composer_line() const override
    {
        if (phase_ == Phase::AddName) {
            return add_name_buffer_;
        }
        if (phase_ == Phase::AddPhone) {
            return add_phone_buffer_;
        }
        if (phase_ == Phase::Search) {
            return query_buffer_;
        }
        return {};
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
        case Phase::MainMenu:
            handle_main_menu_control(key, ctx);
            break;
        case Phase::RecentList:
            handle_recent_list_control(key, ctx);
            break;
        case Phase::AddressBook:
            handle_address_book_control(key, ctx);
            break;
        case Phase::Search:
            handle_search_control(key, ctx);
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
        phase_ = Phase::MainMenu;
        query_buffer_.clear();
        address_book_contacts_.clear();
        action_index_ = 0;
        selected_contact_ = nullptr;
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
        recent_contacts_.clear();
        browse_.clear();
        detail_from_recent_ = false;
    }

    void sync_composer(UiContext &ctx)
    {
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
    }

    void announce_browse_focus(UiContext &ctx, bool at_boundary)
    {
        browse_.announce_focus(ctx.output, at_boundary);
    }

    void load_recent_contacts()
    {
        recent_contacts_.clear();
        for (const std::string &id : load_recent_contact_ids()) {
            const documents::Contact *contact = store_.find_by_id(id);
            if (contact != nullptr) {
                recent_contacts_.push_back(*contact);
            }
        }
    }

    void note_recent_contact(const documents::Contact &contact)
    {
        recent_contacts_.erase(
            std::remove_if(recent_contacts_.begin(), recent_contacts_.end(),
                           [&](const documents::Contact &entry) { return entry.id == contact.id; }),
            recent_contacts_.end());
        recent_contacts_.insert(recent_contacts_.begin(), contact);
        if (recent_contacts_.size() > kMaxRecentContacts) {
            recent_contacts_.resize(kMaxRecentContacts);
        }

        std::vector<std::string> ids;
        ids.reserve(recent_contacts_.size());
        for (const auto &entry : recent_contacts_) {
            ids.push_back(entry.id);
        }
        save_recent_contact_ids(ids);
    }

    void enter_main_menu(UiContext &ctx, const std::string &message)
    {
        phase_ = Phase::MainMenu;
        query_buffer_.clear();
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
        browse_.set_items({"Recently Viewed", "Address Book", "Add New Contact"});
        announce_browse_focus(ctx, false);
        announce(ctx, message);
        if (!browse_.empty()) {
            announce(ctx, browse_.focused_label() + ". " + browse_.position_label());
        }
    }

    void enter_recent_list(UiContext &ctx)
    {
        phase_ = Phase::RecentList;
        std::vector<std::string> labels;
        labels.reserve(recent_contacts_.size());
        for (const auto &contact : recent_contacts_) {
            labels.push_back(contact.name);
        }
        browse_.set_items(std::move(labels));
        if (browse_.empty()) {
            announce(ctx, "No recently viewed contacts. Press Backspace to return.");
            sync_composer(ctx);
            return;
        }
        announce_browse_focus(ctx, false);
        announce(ctx, "Recently viewed.");
    }

    static std::string format_contact_label(const documents::Contact &contact)
    {
        std::string label = contact.name;
        if (!contact.organization.empty()) {
            label += " - " + contact.organization;
        } else if (!contact.phones.empty()) {
            label += " - " + contact.phones.front();
        }
        return label;
    }

    void refresh_address_book_contacts()
    {
        address_book_contacts_ = query_buffer_.empty() ? store_.contacts() : store_.search(query_buffer_);
        std::sort(address_book_contacts_.begin(), address_book_contacts_.end(),
                  [](const documents::Contact &a, const documents::Contact &b) { return a.name < b.name; });
    }

    size_t default_address_book_focus() const
    {
        return address_book_contacts_.empty() ? 0 : 1;
    }

    void rebuild_address_book_labels(size_t focus_index = 0)
    {
        std::vector<std::string> labels;
        labels.reserve(1 + address_book_contacts_.size());
        labels.push_back("Search");
        for (const auto &contact : address_book_contacts_) {
            labels.push_back(format_contact_label(contact));
        }
        browse_.set_items(std::move(labels), focus_index);
    }

    void enter_address_book(UiContext &ctx)
    {
        phase_ = Phase::AddressBook;
        query_buffer_.clear();
        store_.refresh();
        refresh_address_book_contacts();
        rebuild_address_book_labels(default_address_book_focus());
        announce_browse_focus(ctx, false);
        if (address_book_contacts_.empty()) {
            announce(ctx, "Address book. No contacts yet. Select Search or add a contact from the main menu.");
            return;
        }
        announce(ctx, "Address book. " + std::to_string(address_book_contacts_.size()) + " contacts.");
    }

    void enter_search_from_address_book(UiContext &ctx)
    {
        phase_ = Phase::Search;
        browse_.clear();
        sync_composer(ctx);
        announce(ctx, "Search contacts. Type a name and press Enter.");
    }

    void apply_search_and_show_address_book(UiContext &ctx)
    {
        store_.refresh();
        refresh_address_book_contacts();
        phase_ = Phase::AddressBook;
        rebuild_address_book_labels(default_address_book_focus());
        sync_composer(ctx);
        if (address_book_contacts_.empty()) {
            announce(ctx, query_buffer_.empty() ? "No contacts yet."
                                                : "No matches for " + query_buffer_);
            return;
        }
        announce_browse_focus(ctx, false);
        if (!query_buffer_.empty()) {
            announce(ctx, std::to_string(address_book_contacts_.size()) + " matches.");
        }
    }

    void rebuild_action_labels()
    {
        browse_.set_items(std::vector<std::string> {"Copy to document", "Emboss card"}, action_index_);
    }

    void handle_main_menu_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            if (ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const MainMenuKind choice = static_cast<MainMenuKind>(browse_.focus_index());
        switch (choice) {
        case MainMenuKind::RecentlyViewed:
            enter_recent_list(ctx);
            break;
        case MainMenuKind::AddressBook:
            enter_address_book(ctx);
            break;
        case MainMenuKind::AddNewContact:
            start_add_contact(ctx);
            break;
        }
    }

    void handle_address_book_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            return_to_main_menu(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const size_t index = browse_.focus_index();
        if (index == 0) {
            enter_search_from_address_book(ctx);
            return;
        }
        const size_t contact_index = index - 1;
        if (contact_index >= address_book_contacts_.size()) {
            return;
        }
        open_contact(ctx, address_book_contacts_[contact_index]);
    }

    void handle_recent_list_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (browse_.empty()) {
            if (key == keyboard::ControlKey::Backspace) {
                enter_main_menu(ctx, "Contacts");
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            enter_main_menu(ctx, "Contacts");
            return;
        }
        if (key != keyboard::ControlKey::Enter || browse_.focus_index() >= recent_contacts_.size()) {
            return;
        }
        open_contact(ctx, recent_contacts_[browse_.focus_index()]);
    }

    void start_add_contact(UiContext &ctx)
    {
        phase_ = Phase::AddName;
        add_name_buffer_.clear();
        add_phone_buffer_.clear();
        browse_.clear();
        announce(ctx, "Add contact. Enter name.");
        sync_composer(ctx);
    }

    void return_to_main_menu(UiContext &ctx)
    {
        load_recent_contacts();
        enter_main_menu(ctx, "Contacts");
    }

    void handle_add_name_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!add_name_buffer_.empty()) {
                add_name_buffer_.pop_back();
                sync_composer(ctx);
                return;
            }
            return_to_main_menu(ctx);
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
        return_to_main_menu(ctx);
    }

    void handle_search_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                sync_composer(ctx);
                return;
            }
            apply_search_and_show_address_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        store_.refresh();
        refresh_address_book_contacts();
        if (address_book_contacts_.size() == 1) {
            open_contact(ctx, address_book_contacts_.front());
            return;
        }
        apply_search_and_show_address_book(ctx);
    }

    void handle_detail_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            browse_.clear();
            if (detail_from_recent_) {
                enter_recent_list(ctx);
            } else {
                apply_search_and_show_address_book(ctx);
            }
            selected_contact_ = nullptr;
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        action_index_ = 0;
        phase_ = Phase::Actions;
        rebuild_action_labels();
        announce_browse_focus(ctx, false);
        announce_action(ctx);
    }

    void handle_actions_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Detail;
            browse_.clear();
            sync_composer(ctx);
            announce_contact(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            const bool moved = browse_.move_up();
            if (moved) {
                action_index_ = browse_.focus_index();
                announce_action(ctx);
            }
            announce_browse_focus(ctx, !moved);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            const bool moved = browse_.move_down();
            if (moved) {
                action_index_ = browse_.focus_index();
                announce_action(ctx);
            }
            announce_browse_focus(ctx, !moved);
            return;
        }
        if (key != keyboard::ControlKey::Enter || selected_contact_ == nullptr) {
            return;
        }

        action_index_ = browse_.focus_index();
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
        detail_from_recent_ = phase_ == Phase::RecentList;
        note_recent_contact(*selected_contact_);
        phase_ = Phase::Detail;
        browse_.clear();
        sync_composer(ctx);
        announce_contact(ctx);
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
        for (char ch : card) {
            ctx.brf->append_char(ch);
        }
        ctx.brf->save();
        announce(ctx, "Copied contact to document");
        phase_ = Phase::Detail;
        browse_.clear();
        sync_composer(ctx);
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
        browse_.clear();
        sync_composer(ctx);
    }

    documents::ContactsConfig config_ =
        documents::load_contacts_config("/etc/braillatron/contacts.conf");
    documents::ContactsStore store_ {config_};
    Phase phase_ = Phase::MainMenu;
    std::string query_buffer_;
    std::string add_name_buffer_;
    std::string add_phone_buffer_;
    std::vector<documents::Contact> address_book_contacts_;
    std::vector<documents::Contact> recent_contacts_;
    const documents::Contact *selected_contact_ = nullptr;
    size_t action_index_ = 0;
    bool detail_from_recent_ = false;
    LayeredBrowseList browse_;
};

} // namespace

std::unique_ptr<AppSession> make_contacts_app()
{
    return std::make_unique<ContactsApp>();
}

} // namespace braillatron::ui
