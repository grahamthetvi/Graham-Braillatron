#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../../documents/library_store.h"
#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"
#include "../layered_browse_list.h"
#include "app_session.h"
#include "app_util.h"
#include "held_audio_skip.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

constexpr size_t kMaxAnnounceLen = 300;
constexpr size_t kMaxBrowseLabelLen = 72;

std::string truncate_for_tts(const std::string &text)
{
    if (text.size() <= kMaxAnnounceLen) {
        return text;
    }
    return text.substr(0, kMaxAnnounceLen) + "...";
}

std::string truncate_for_browse(const std::string &text)
{
    if (text.size() <= kMaxBrowseLabelLen) {
        return text;
    }
    return text.substr(0, kMaxBrowseLabelLen - 3) + "...";
}

std::vector<std::string> wrap_text_lines(const std::string &text, size_t max_len = kMaxBrowseLabelLen)
{
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }

    size_t pos = 0;
    while (pos < text.size()) {
        if (text.size() - pos <= max_len) {
            lines.push_back(text.substr(pos));
            break;
        }
        size_t break_at = text.rfind(' ', pos + max_len);
        if (break_at == std::string::npos || break_at <= pos) {
            break_at = pos + max_len;
        }
        lines.push_back(text.substr(pos, break_at - pos));
        pos = break_at;
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
    }
    return lines;
}

struct SearchResult {
    std::string id;
    std::string source;
    std::string title;
    std::string author;
    std::string detail;
};

struct WorthwhileResult {
    std::string id;
    std::string title;
    std::string kind;
};

struct UsbEntry {
    std::string path;
    std::string label;
    bool is_directory = false;
};

enum class Phase {
    Menu,
    PublicSources,
    LocalList,
    AudioPlaying,
    RemovePick,
    RemoveConfirm,
    RenamePick,
    RenameEntry,
    UsbDrivePick,
    UsbBrowse,
    SearchQuery,
    SearchResults,
    Downloading,
    Reading,
    ReadingText,
    PrintScope,
    WorthwhileMenu,
    WorthwhileSearch,
    WorthwhileResults,
    WorthwhileDownloading,
};

enum class MenuChoice {
    LocalLibrary,
    PublicSources,
    WorthwhileSecret,
};

enum class PublicSourceChoice {
    Gutendex,
    OpenLibrary,
    LibriVox,
};

enum class WorthwhileMenuChoice {
    SearchMovies,
    SearchShows,
    RecentMovies,
    RecentShows,
};

enum class PrintScopeChoice {
    CurrentPage,
    ThisSection,
    WholeBook,
};

class LibraryApp final : public AppSession {
public:
    std::string id() const override { return "library"; }
    std::string label() const override { return "Library"; }
    AppKind kind() const override { return AppKind::Standalone; }

    LibraryApp()
        : config_(documents::load_library_store_config("/etc/braillatron/library.conf"))
        , store_(config_)
    {
    }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        store_.refresh();
        phase_ = Phase::Menu;
        rebuild_browse();
        sync_chrome(ctx);
        announce_browse_focus(ctx, false);
        announce(ctx, "Library. Local books, public sources, or Worthwhile Secret.");
    }

    void on_exit(UiContext &ctx) override
    {
        save_reading_progress();
        reset_session();
        announce(ctx, "Library closed");
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "music.ended") {
            if (phase_ == Phase::AudioPlaying) {
                phase_ = Phase::LocalList;
                rebuild_browse();
                sync_chrome(ctx);
            }
            now_playing_audio_.clear();
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        }
        if (event.type == "music.playing" && ctx.output != nullptr) {
            ctx.output->set_media_playing(true);
            ctx.output->set_media_paused(false);
        }
    }

    bool menu_has_remove() const override
    {
        return phase_ == Phase::LocalList && !local_books_.empty();
    }

    bool menu_has_rename() const override
    {
        return phase_ == Phase::LocalList && !local_books_.empty();
    }

    bool menu_has_import_usb() const override
    {
        return phase_ == Phase::LocalList;
    }

    void on_poll(UiContext &ctx) override
    {
        if (phase_ == Phase::AudioPlaying && ctx.connect != nullptr) {
            held_skip_.poll(now_ms(), ctx.connect);
        }
        if (pending_refresh_local_) {
            pending_refresh_local_ = false;
            store_.refresh();
            local_books_ = store_.books();
            if (phase_ == Phase::Downloading) {
                phase_ = Phase::LocalList;
                browse_.set_focus(0);
                rebuild_browse();
                sync_chrome(ctx);
                if (local_books_.empty()) {
                    pending_announce_ = "Download complete but book not found locally.";
                } else {
                    pending_announce_ = "Download complete. " + std::to_string(local_books_.size()) +
                                          " books available.";
                }
            }
        }
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
            sync_chrome(ctx);
            if (phase_ == Phase::LocalList && !local_books_.empty()) {
                announce_local_book(ctx);
            } else if (phase_ == Phase::WorthwhileResults && !worthwhile_results_.empty()) {
                announce_worthwhile_result(ctx);
            }
        }
    }

    void on_chord(uint8_t dot_mask, UiContext &) override
    {
        if (phase_ == Phase::AudioPlaying && is_skip_chord(dot_mask)) {
            return;
        }
    }

    bool buffers_braille_words() const override
    {
        return phase_ == Phase::SearchQuery || phase_ == Phase::WorthwhileSearch ||
               phase_ == Phase::RenameEntry;
    }

    bool buffers_uncontracted_braille_words() const override
    {
        // Public-source search must stay literal ASCII (G2 was turning "cats" into "cas.").
        return phase_ == Phase::SearchQuery || phase_ == Phase::WorthwhileSearch;
    }

    bool browse_list_active() const override
    {
        return phase_ == Phase::Menu || phase_ == Phase::PublicSources || phase_ == Phase::LocalList ||
               phase_ == Phase::RemovePick || phase_ == Phase::RemoveConfirm ||
               phase_ == Phase::RenamePick || phase_ == Phase::UsbDrivePick ||
               phase_ == Phase::UsbBrowse || phase_ == Phase::SearchResults ||
               phase_ == Phase::Reading || phase_ == Phase::ReadingText || phase_ == Phase::PrintScope ||
               phase_ == Phase::WorthwhileMenu || phase_ == Phase::WorthwhileResults;
    }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string composer_line() const override
    {
        if (phase_ == Phase::SearchQuery) {
            return query_buffer_;
        }
        if (phase_ == Phase::WorthwhileSearch) {
            return worthwhile_query_;
        }
        if (phase_ == Phase::RenameEntry) {
            return rename_buffer_;
        }
        return {};
    }

    std::vector<std::string> browse_items() const override { return browse_.labels(); }

    size_t browse_focus_index() const override { return browse_.focus_index(); }

    std::string browse_breadcrumb() const override
    {
        if (phase_ == Phase::ReadingText) {
            return section_title_.empty() ? "Reading" : section_title_;
        }
        if (phase_ == Phase::PrintScope) {
            return "Print";
        }
        return breadcrumb_;
    }

    void on_menu_action(const std::string &action, UiContext &ctx) override
    {
        if (action == "remove") {
            if (phase_ != Phase::LocalList || local_books_.empty()) {
                announce(ctx, "No items to remove");
                return;
            }
            phase_before_pick_ = phase_;
            phase_ = Phase::RemovePick;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Select item to remove. Enter to choose. Backspace to cancel.");
            announce_local_book(ctx);
            return;
        }
        if (action == "rename") {
            if (phase_ != Phase::LocalList || local_books_.empty()) {
                announce(ctx, "No items to rename");
                return;
            }
            phase_before_pick_ = phase_;
            phase_ = Phase::RenamePick;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Select item to rename. Enter to choose. Backspace to cancel.");
            announce_local_book(ctx);
            return;
        }
        if (action == "import_usb") {
            if (phase_ != Phase::LocalList) {
                announce(ctx, "Open local library first");
                return;
            }
            usb_mounts_ = store_.list_removable_mounts();
            if (usb_mounts_.empty()) {
                announce(ctx, "No USB drive found. Insert a flash drive and try again.");
                return;
            }
            phase_before_pick_ = phase_;
            phase_ = Phase::UsbDrivePick;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Select USB drive. Enter to browse. Backspace to cancel.");
            announce_browse_focus(ctx, false);
            return;
        }
        if (action != "print") {
            return;
        }
        if (phase_ != Phase::Reading && phase_ != Phase::ReadingText) {
            announce(ctx, "Open a book first");
            return;
        }
        if (document_ == nullptr) {
            announce(ctx, "Nothing to print");
            return;
        }
        phase_before_print_ = phase_;
        phase_ = Phase::PrintScope;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Print scope. Current page, this section, or whole book.");
        announce_browse_focus(ctx, false);
    }

    void announce_browse_focus(UiContext &ctx, bool at_boundary)
    {
        browse_.set_container_name(label());
        browse_.announce_focus(ctx.output, at_boundary);
    }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        if (phase_ != Phase::SearchQuery && phase_ != Phase::WorthwhileSearch &&
            phase_ != Phase::RenameEntry) {
            return;
        }
        if (phase_ == Phase::SearchQuery) {
            query_buffer_ += text;
        } else if (phase_ == Phase::WorthwhileSearch) {
            worthwhile_query_ += text;
        } else {
            rename_buffer_ += text;
        }
        sync_chrome(ctx);
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        switch (phase_) {
        case Phase::Menu:
            handle_menu(key, ctx);
            break;
        case Phase::PublicSources:
            handle_public_sources(key, ctx);
            break;
        case Phase::LocalList:
            handle_local_list(key, ctx);
            break;
        case Phase::RemovePick:
            handle_remove_pick(key, ctx);
            break;
        case Phase::RemoveConfirm:
            handle_remove_confirm(key, ctx);
            break;
        case Phase::RenamePick:
            handle_rename_pick(key, ctx);
            break;
        case Phase::RenameEntry:
            handle_rename_entry(key, ctx);
            break;
        case Phase::UsbDrivePick:
            handle_usb_drive_pick(key, ctx);
            break;
        case Phase::UsbBrowse:
            handle_usb_browse(key, ctx);
            break;
        case Phase::AudioPlaying:
            handle_audio_playing(key, ctx);
            break;
        case Phase::SearchQuery:
            handle_search_query(key, ctx);
            break;
        case Phase::SearchResults:
            handle_search_results(key, ctx);
            break;
        case Phase::Downloading:
            if (key == keyboard::ControlKey::Backspace) {
                enter_menu(ctx);
            }
            break;
        case Phase::Reading:
            handle_reading(key, ctx);
            break;
        case Phase::ReadingText:
            handle_reading_text(key, ctx);
            break;
        case Phase::PrintScope:
            handle_print_scope(key, ctx);
            break;
        case Phase::WorthwhileMenu:
            handle_worthwhile_menu(key, ctx);
            break;
        case Phase::WorthwhileSearch:
            handle_worthwhile_search(key, ctx);
            break;
        case Phase::WorthwhileResults:
            handle_worthwhile_results(key, ctx);
            break;
        case Phase::WorthwhileDownloading:
            if (key == keyboard::ControlKey::Backspace) {
                enter_menu(ctx);
            }
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Menu;
        browse_.clear();
        breadcrumb_.clear();
        local_books_.clear();
        search_results_.clear();
        worthwhile_results_.clear();
        query_buffer_.clear();
        worthwhile_query_.clear();
        worthwhile_kind_.clear();
        worthwhile_mode_recent_ = false;
        active_source_.clear();
        active_source_label_.clear();
        pending_announce_.clear();
        pending_refresh_local_ = false;
        current_book_ = nullptr;
        document_.reset();
        section_index_ = 0;
        section_lines_.clear();
        section_line_index_ = 0;
        section_title_.clear();
        phase_before_print_ = Phase::Reading;
        now_playing_audio_.clear();
        held_skip_.reset();
        pending_remove_index_ = 0;
        pending_rename_index_ = 0;
        rename_buffer_.clear();
        usb_mounts_.clear();
        usb_entries_.clear();
        usb_browse_path_.clear();
        phase_before_pick_ = Phase::LocalList;
    }

    static uint64_t now_ms()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void enter_menu(UiContext &ctx)
    {
        phase_ = Phase::Menu;
        rebuild_browse();
        sync_chrome(ctx);
        announce_menu(ctx);
    }

    void rebuild_browse()
    {
        std::vector<std::string> items;
        switch (phase_) {
        case Phase::Menu:
            breadcrumb_.clear();
            items = {"Local library", "Public sources", "Worthwhile Secret"};
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::PublicSources:
            breadcrumb_ = "Public sources";
            items = {"Project Gutenberg", "Open Library", "LibriVox"};
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::LocalList:
            breadcrumb_ = "Local library";
            items.reserve(local_books_.size());
            for (const auto &book : local_books_) {
                items.push_back(format_book_label(book));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::RemovePick:
            breadcrumb_ = "Remove item";
            items.reserve(local_books_.size());
            for (const auto &book : local_books_) {
                items.push_back(format_book_label(book));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::RemoveConfirm:
            breadcrumb_ = "Confirm remove";
            if (pending_remove_index_ < local_books_.size()) {
                items = {"Remove " + local_books_[pending_remove_index_].title + "?"};
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::RenamePick:
            breadcrumb_ = "Rename item";
            items.reserve(local_books_.size());
            for (const auto &book : local_books_) {
                items.push_back(format_book_label(book));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::RenameEntry:
            breadcrumb_ = "New name";
            browse_.clear();
            break;
        case Phase::UsbDrivePick:
            breadcrumb_ = "USB drives";
            items.reserve(usb_mounts_.size());
            for (const auto &mount : usb_mounts_) {
                items.push_back(truncate_for_browse(mount));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::UsbBrowse:
            breadcrumb_ = truncate_for_browse(usb_browse_path_);
            items.reserve(usb_entries_.size());
            for (const auto &entry : usb_entries_) {
                items.push_back(truncate_for_browse(entry.label));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::SearchQuery:
            breadcrumb_ = active_source_label_.empty() ? "Search" : active_source_label_;
            browse_.clear();
            break;
        case Phase::SearchResults:
            breadcrumb_ = active_source_label_.empty() ? "Search results" : active_source_label_;
            items.reserve(search_results_.size());
            for (const auto &result : search_results_) {
                items.push_back(format_search_label(result));
            }
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::Reading:
            breadcrumb_ = current_book_ != nullptr ? current_book_->title : "Reading";
            if (document_ != nullptr) {
                items.reserve(document_->sections().size());
                for (const auto &section : document_->sections()) {
                    items.push_back(section.title.empty() ? "Untitled section" : section.title);
                }
            }
            browse_.set_items(std::move(items), static_cast<size_t>(section_index_));
            break;
        case Phase::ReadingText:
            breadcrumb_ = section_title_.empty() ? "Reading" : section_title_;
            items.reserve(section_lines_.size());
            for (const auto &line : section_lines_) {
                items.push_back(truncate_for_browse(line));
            }
            browse_.set_items(std::move(items), section_line_index_);
            break;
        case Phase::PrintScope:
            breadcrumb_ = "Print";
            items = {"Current page", "This section", "Whole book"};
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::WorthwhileMenu:
            breadcrumb_ = "Worthwhile Secret";
            items = {"Search movies", "Search shows", "Recent movies", "Recent shows"};
            browse_.set_items(std::move(items), 0);
            break;
        case Phase::WorthwhileSearch:
            breadcrumb_ = "Worthwhile Secret";
            browse_.clear();
            break;
        case Phase::WorthwhileResults:
            breadcrumb_ = "Worthwhile Secret";
            items.reserve(worthwhile_results_.size());
            for (const auto &result : worthwhile_results_) {
                items.push_back(result.title);
            }
            browse_.set_items(std::move(items), 0);
            break;
        default:
            breadcrumb_.clear();
            browse_.clear();
            break;
        }
    }

    static bool is_audio_format(const std::string &format)
    {
        return format == "mp3" || format == "m4a" || format == "m4b" || format == "ogg" ||
               format == "flac" || format == "wav" || format == "aac";
    }

    static std::string format_book_label(const documents::LibraryBook &book)
    {
        std::string label;
        if (book.author.empty()) {
            label = book.title;
        } else {
            label = book.title + " - " + book.author;
        }
        if (is_audio_format(book.format)) {
            label += " (audio)";
        }
        return label;
    }

    static std::string format_search_label(const SearchResult &result)
    {
        std::string label = result.title;
        if (!result.author.empty()) {
            label += " - " + result.author;
        }
        if (!result.detail.empty()) {
            label += " (" + result.detail + ")";
        }
        return label;
    }

    void begin_source_search(UiContext &ctx, const std::string &source_key,
                             const std::string &source_label)
    {
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable for " + source_label + ".");
            return;
        }
        active_source_ = source_key;
        active_source_label_ = source_label;
        phase_ = Phase::SearchQuery;
        query_buffer_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, source_label + ". Type title or author and press Enter.");
    }

    void announce_menu(UiContext &ctx)
    {
        announce_browse_focus(ctx, false);
    }

    void announce_browse_item(UiContext &ctx, const std::string &detail)
    {
        announce_browse_focus(ctx, false);
        if (!detail.empty()) {
            announce(ctx, detail);
        }
    }

    void handle_menu(keyboard::ControlKey key, UiContext &ctx)
    {
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

        if (browse_.focus_index() == static_cast<size_t>(MenuChoice::LocalLibrary)) {
            store_.refresh();
            local_books_ = store_.books();
            if (local_books_.empty()) {
                announce(ctx, "No local books. Browse public sources or import EPUB via LocalSend.");
                return;
            }
            phase_ = Phase::LocalList;
            rebuild_browse();
            sync_chrome(ctx);
            announce_local_book(ctx);
            return;
        }

        if (browse_.focus_index() == static_cast<size_t>(MenuChoice::PublicSources)) {
            phase_ = Phase::PublicSources;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Public sources. Project Gutenberg, Open Library, LibriVox.");
            announce_browse_focus(ctx, false);
            return;
        }

        if (browse_.focus_index() == static_cast<size_t>(MenuChoice::WorthwhileSecret)) {
            if (ctx.connect == nullptr) {
                announce(ctx, "Connectivity unavailable for Worthwhile Secret.");
                return;
            }
            phase_ = Phase::WorthwhileMenu;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Worthwhile Secret. Search or browse recent audio.");
            announce_browse_focus(ctx, false);
        }
    }

    void handle_public_sources(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            enter_menu(ctx);
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

        switch (browse_.focus_index()) {
        case static_cast<size_t>(PublicSourceChoice::Gutendex):
            begin_source_search(ctx, "gutendex", "Project Gutenberg");
            break;
        case static_cast<size_t>(PublicSourceChoice::OpenLibrary):
            begin_source_search(ctx, "openlibrary", "Open Library");
            break;
        case static_cast<size_t>(PublicSourceChoice::LibriVox):
            begin_source_search(ctx, "librivox", "LibriVox");
            break;
        default:
            break;
        }
    }

    void handle_local_list(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            enter_menu(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || local_books_.empty()) {
            return;
        }
        open_book(ctx, local_books_[browse_.focus_index()]);
    }

    void handle_remove_pick(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = phase_before_pick_;
            rebuild_browse();
            sync_chrome(ctx);
            if (phase_ == Phase::LocalList && !local_books_.empty()) {
                announce_local_book(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || local_books_.empty()) {
            return;
        }
        pending_remove_index_ = browse_.focus_index();
        phase_ = Phase::RemoveConfirm;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Remove " + local_books_[pending_remove_index_].title +
                           "? Enter to confirm. Backspace to cancel.");
    }

    void handle_remove_confirm(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::RemovePick;
            rebuild_browse();
            sync_chrome(ctx);
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || pending_remove_index_ >= local_books_.size()) {
            return;
        }
        finalize_remove(ctx, local_books_[pending_remove_index_]);
    }

    void finalize_remove(UiContext &ctx, const documents::LibraryBook &book)
    {
        if (!store_.remove_book(book.id)) {
            announce(ctx, "Could not remove " + book.title);
            return;
        }
        if (phase_ == Phase::AudioPlaying && now_playing_audio_ == book.local_path) {
            if (ctx.connect != nullptr) {
                ctx.connect->request("music.stop");
            }
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
            now_playing_audio_.clear();
        }
        store_.refresh();
        local_books_ = store_.books();
        phase_ = Phase::LocalList;
        if (local_books_.empty()) {
            enter_menu(ctx);
            announce(ctx, "Removed " + book.title + ". Library is empty.");
            return;
        }
        if (browse_.focus_index() >= local_books_.size()) {
            browse_.set_focus(local_books_.size() - 1);
        }
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Removed " + book.title + ".");
        announce_local_book(ctx);
    }

    void handle_rename_pick(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = phase_before_pick_;
            rebuild_browse();
            sync_chrome(ctx);
            if (phase_ == Phase::LocalList && !local_books_.empty()) {
                announce_local_book(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || local_books_.empty()) {
            return;
        }
        pending_rename_index_ = browse_.focus_index();
        rename_buffer_ = local_books_[pending_rename_index_].title;
        phase_ = Phase::RenameEntry;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Enter new name for " + local_books_[pending_rename_index_].title +
                           ". Press Enter when done. Backspace to cancel.");
    }

    void handle_rename_entry(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!rename_buffer_.empty()) {
                rename_buffer_.pop_back();
                sync_chrome(ctx);
                return;
            }
            phase_ = Phase::RenamePick;
            rebuild_browse();
            sync_chrome(ctx);
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || pending_rename_index_ >= local_books_.size()) {
            return;
        }
        const std::string trimmed = rename_buffer_;
        if (trimmed.empty()) {
            announce(ctx, "Enter a name.");
            return;
        }
        const documents::LibraryBook &book = local_books_[pending_rename_index_];
        if (!store_.rename_book(book.id, trimmed)) {
            announce(ctx, "Could not rename " + book.title);
            return;
        }
        store_.refresh();
        local_books_ = store_.books();
        phase_ = Phase::LocalList;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Renamed to " + trimmed + ".");
        announce_local_book(ctx);
    }

    void refresh_usb_entries()
    {
        usb_entries_.clear();
        std::error_code ec;
        if (usb_browse_path_.empty()) {
            return;
        }

        const std::filesystem::path current(usb_browse_path_);
        if (current.has_parent_path() && current != current.root_path()) {
            UsbEntry parent;
            parent.path = current.parent_path().string();
            parent.label = "..";
            parent.is_directory = true;
            usb_entries_.push_back(std::move(parent));
        }

        for (const auto &entry : std::filesystem::directory_iterator(usb_browse_path_, ec)) {
            UsbEntry item;
            item.path = entry.path().string();
            item.is_directory = entry.is_directory(ec);
            item.label = entry.path().filename().string();
            if (item.is_directory) {
                item.label += "/";
            }
            usb_entries_.push_back(std::move(item));
        }

        std::sort(usb_entries_.begin() + (usb_entries_.empty() ? 0 : 1), usb_entries_.end(),
                  [](const UsbEntry &a, const UsbEntry &b) {
                      if (a.is_directory != b.is_directory) {
                          return a.is_directory;
                      }
                      return a.label < b.label;
                  });
    }

    void handle_usb_drive_pick(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = phase_before_pick_;
            rebuild_browse();
            sync_chrome(ctx);
            if (phase_ == Phase::LocalList && !local_books_.empty()) {
                announce_local_book(ctx);
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
        if (key != keyboard::ControlKey::Enter || usb_mounts_.empty()) {
            return;
        }
        usb_browse_path_ = usb_mounts_[browse_.focus_index()];
        refresh_usb_entries();
        phase_ = Phase::UsbBrowse;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "USB folder " + usb_browse_path_ + ". Enter to open or import.");
        if (!usb_entries_.empty()) {
            announce(ctx, usb_entries_.front().label);
        }
    }

    void handle_usb_browse(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (usb_mounts_.size() > 1) {
                phase_ = Phase::UsbDrivePick;
                usb_browse_path_.clear();
                usb_entries_.clear();
                rebuild_browse();
                sync_chrome(ctx);
                announce_browse_focus(ctx, false);
                return;
            }
            phase_ = phase_before_pick_;
            usb_browse_path_.clear();
            usb_entries_.clear();
            rebuild_browse();
            sync_chrome(ctx);
            if (phase_ == Phase::LocalList && !local_books_.empty()) {
                announce_local_book(ctx);
            }
            return;
        }
        if (usb_entries_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce(ctx, usb_entries_[browse_.focus_index()].label);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce(ctx, usb_entries_[browse_.focus_index()].label);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const UsbEntry &entry = usb_entries_[browse_.focus_index()];
        if (entry.is_directory) {
            usb_browse_path_ = entry.path;
            refresh_usb_entries();
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Folder " + entry.label);
            if (!usb_entries_.empty()) {
                announce(ctx, usb_entries_.front().label);
            }
            return;
        }

        if (store_.import_file(entry.path)) {
            store_.refresh();
            local_books_ = store_.books();
            phase_ = Phase::LocalList;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Imported " + entry.label + ".");
            if (!local_books_.empty()) {
                announce_local_book(ctx);
            }
            return;
        }
        announce(ctx, "Could not import " + entry.label +
                           ". Supported: audio, text, BRF, EPUB.");
    }

    void handle_search_query(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                sync_chrome(ctx);
                return;
            }
            enter_public_sources(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (query_buffer_.empty()) {
            announce(ctx, "Enter a search term.");
            return;
        }
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        phase_ = Phase::SearchResults;
        search_results_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Searching " + active_source_label_ + " for " + query_buffer_ + ".");

        const std::string payload =
            "\"query\":\"" + braillatron::connect::json_escape(query_buffer_) + "\",\"source\":\"" +
            braillatron::connect::json_escape(active_source_) + "\"";
        ctx.connect->request_async("library.search", payload,
                                   [this](const std::string &response) { parse_search_response(response); });
    }

    void enter_public_sources(UiContext &ctx)
    {
        phase_ = Phase::PublicSources;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Public sources.");
        announce_browse_focus(ctx, false);
    }

    void handle_search_results(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::SearchQuery;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (search_results_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce_search_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce_search_result(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        const SearchResult &result = search_results_[browse_.focus_index()];
        phase_ = Phase::Downloading;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Downloading " + result.title + ".");

        ctx.connect->request_async(
            "library.download",
            "\"source\":\"" + braillatron::connect::json_escape(result.source) + "\",\"id\":\"" +
                braillatron::connect::json_escape(result.id) + "\"",
            [this, result](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    pending_refresh_local_ = true;
                    pending_announce_ = "Downloaded " + result.title + ".";
                } else {
                    pending_announce_ = "Download failed for " + result.title + ".";
                    phase_ = Phase::SearchResults;
                    rebuild_browse();
                }
            });
    }

    void enter_worthwhile_menu(UiContext &ctx)
    {
        phase_ = Phase::WorthwhileMenu;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Worthwhile Secret.");
        announce_browse_focus(ctx, false);
    }

    void begin_worthwhile_search(UiContext &ctx, const std::string &kind, bool recent)
    {
        worthwhile_kind_ = kind;
        worthwhile_mode_recent_ = recent;
        if (recent) {
            phase_ = Phase::WorthwhileResults;
            worthwhile_results_.clear();
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Loading recent " + kind + ".");
            ctx.connect->request_async(
                "worthwhile.recent", "\"kind\":\"" + braillatron::connect::json_escape(kind) + "\"",
                [this](const std::string &response) { parse_worthwhile_response(response); });
            return;
        }
        phase_ = Phase::WorthwhileSearch;
        worthwhile_query_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Search " + kind + ". Type a title and press Enter.");
    }

    void handle_worthwhile_menu(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            enter_menu(ctx);
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
        if (key != keyboard::ControlKey::Enter || ctx.connect == nullptr) {
            return;
        }

        switch (browse_.focus_index()) {
        case static_cast<size_t>(WorthwhileMenuChoice::SearchMovies):
            begin_worthwhile_search(ctx, "movies", false);
            break;
        case static_cast<size_t>(WorthwhileMenuChoice::SearchShows):
            begin_worthwhile_search(ctx, "shows", false);
            break;
        case static_cast<size_t>(WorthwhileMenuChoice::RecentMovies):
            begin_worthwhile_search(ctx, "movies", true);
            break;
        case static_cast<size_t>(WorthwhileMenuChoice::RecentShows):
            begin_worthwhile_search(ctx, "shows", true);
            break;
        default:
            break;
        }
    }

    void handle_worthwhile_search(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!worthwhile_query_.empty()) {
                worthwhile_query_.pop_back();
                sync_chrome(ctx);
                return;
            }
            enter_worthwhile_menu(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (worthwhile_query_.empty()) {
            announce(ctx, "Enter a search term.");
            return;
        }
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        phase_ = Phase::WorthwhileResults;
        worthwhile_results_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Searching for " + worthwhile_query_ + ".");

        const std::string payload =
            "\"query\":\"" + braillatron::connect::json_escape(worthwhile_query_) + "\",\"kind\":\"" +
            braillatron::connect::json_escape(worthwhile_kind_) + "\"";
        ctx.connect->request_async("worthwhile.search", payload,
                                   [this](const std::string &response) {
                                       parse_worthwhile_response(response);
                                   });
    }

    void handle_worthwhile_results(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (worthwhile_mode_recent_) {
                enter_worthwhile_menu(ctx);
            } else {
                phase_ = Phase::WorthwhileSearch;
                rebuild_browse();
                sync_chrome(ctx);
                announce(ctx, "Search. " + worthwhile_query_);
            }
            return;
        }
        if (worthwhile_results_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            announce_worthwhile_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            announce_worthwhile_result(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || ctx.connect == nullptr) {
            return;
        }

        const WorthwhileResult &result = worthwhile_results_[browse_.focus_index()];
        phase_ = Phase::WorthwhileDownloading;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Downloading " + result.title + ".");

        ctx.connect->request_async(
            "worthwhile.download",
            "\"item_id\":\"" + braillatron::connect::json_escape(result.id) + "\"",
            [this, result](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    const std::string path =
                        braillatron::connect::json_get_string(response, "path");
                    if (!path.empty()) {
                        store_.register_media_file(path, result.title, "worthwhile");
                        pending_refresh_local_ = true;
                    }
                    pending_announce_ = "Downloaded " + result.title + ". Saved to library.";
                    phase_ = Phase::WorthwhileResults;
                    rebuild_browse();
                } else {
                    pending_announce_ = "Download failed for " + result.title + ".";
                    phase_ = Phase::WorthwhileResults;
                    rebuild_browse();
                }
            });
    }

    void parse_worthwhile_response(const std::string &response)
    {
        worthwhile_results_.clear();
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = worthwhile_mode_recent_ ? "Recent list failed." : "Search failed.";
            phase_ = worthwhile_mode_recent_ ? Phase::WorthwhileMenu : Phase::WorthwhileSearch;
            rebuild_browse();
            return;
        }

        const std::string body = braillatron::connect::json_get_array_body(response, "results");
        for (const auto &obj : braillatron::connect::json_split_objects("[" + body + "]")) {
            WorthwhileResult result;
            result.id = braillatron::connect::json_get_string(obj, "id");
            result.title = braillatron::connect::json_get_string(obj, "title");
            result.kind = braillatron::connect::json_get_string(obj, "kind");
            if (result.kind.empty()) {
                result.kind = worthwhile_kind_;
            }
            if (!result.id.empty() && !result.title.empty()) {
                worthwhile_results_.push_back(std::move(result));
            }
        }

        if (worthwhile_results_.empty()) {
            pending_announce_ = worthwhile_mode_recent_ ? "No recent items found."
                                                        : "No results for " + worthwhile_query_ + ".";
            phase_ = worthwhile_mode_recent_ ? Phase::WorthwhileMenu : Phase::WorthwhileSearch;
            rebuild_browse();
            return;
        }

        phase_ = Phase::WorthwhileResults;
        rebuild_browse();
        pending_announce_ = "Found " + std::to_string(worthwhile_results_.size()) + " results. 1. " +
                            worthwhile_results_.front().title;
    }

    void announce_worthwhile_result(UiContext &ctx)
    {
        const WorthwhileResult &result = worthwhile_results_[browse_.focus_index()];
        announce_browse_item(ctx, result.title);
    }

    void handle_reading(keyboard::ControlKey key, UiContext &ctx)
    {
        if (document_ == nullptr || document_->sections().empty()) {
            phase_ = Phase::LocalList;
            rebuild_browse();
            sync_chrome(ctx);
            return;
        }

        const auto &sections = document_->sections();
        if (key == keyboard::ControlKey::Backspace) {
            save_reading_progress();
            phase_ = Phase::LocalList;
            rebuild_browse();
            sync_chrome(ctx);
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            if (section_index_ > 0) {
                --section_index_;
                browse_.set_focus(static_cast<size_t>(section_index_));
                announce_section(ctx);
                save_reading_progress();
            } else {
                announce_browse_focus(ctx, true);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            section_index_ + 1 < static_cast<int>(sections.size())) {
            ++section_index_;
            browse_.set_focus(static_cast<size_t>(section_index_));
            announce_section(ctx);
            save_reading_progress();
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, true);
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            read_section_text(ctx);
        }
    }

    void parse_search_response(const std::string &response)
    {
        search_results_.clear();
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Search failed.";
            phase_ = Phase::SearchQuery;
            rebuild_browse();
            return;
        }

        const std::string body = braillatron::connect::json_get_array_body(response, "results");
        for (const auto &obj : braillatron::connect::json_split_objects("[" + body + "]")) {
            SearchResult result;
            result.id = braillatron::connect::json_get_string(obj, "id");
            result.source = braillatron::connect::json_get_string(obj, "source");
            result.title = braillatron::connect::json_get_string(obj, "title");
            result.author = braillatron::connect::json_get_string(obj, "author");
            result.detail = braillatron::connect::json_get_string(obj, "detail");
            if (result.source.empty()) {
                result.source = active_source_;
            }
            if (!result.id.empty() && !result.title.empty()) {
                search_results_.push_back(std::move(result));
            }
        }

        if (search_results_.empty()) {
            pending_announce_ = "No results for " + query_buffer_ + ".";
            phase_ = Phase::SearchQuery;
            rebuild_browse();
            return;
        }

        phase_ = Phase::SearchResults;
        rebuild_browse();
        pending_announce_ = "Found " + std::to_string(search_results_.size()) + " results. 1. " +
                            search_results_.front().title + " by " + search_results_.front().author;
    }

    void open_book(UiContext &ctx, const documents::LibraryBook &book)
    {
        if (is_audio_format(book.format)) {
            if (ctx.connect == nullptr) {
                announce(ctx, "Connectivity unavailable");
                return;
            }
            const std::string response = ctx.connect->request(
                "music.play_path",
                "\"path\":\"" + braillatron::connect::json_escape(book.local_path) + "\"");
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                announce(ctx, "Playback failed for " + book.title);
                return;
            }
            phase_ = Phase::AudioPlaying;
            now_playing_audio_ = book.local_path;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
                ctx.output->set_media_paused(false);
            }
            rebuild_browse();
            sync_chrome(ctx);
            announce_over_media(ctx, "Playing " + book.title +
                               ". Enter pause. Hold dots 1-2-3 skip back, 4-5-6 skip forward. "
                               "Backspace returns to list. Hold Shift to hear speech while playing. "
                               "Menu to stop.");
            return;
        }

        auto doc = std::make_unique<documents::EbookDocument>();
        if (!doc->open(book.local_path)) {
            announce(ctx, "Could not open " + book.title + ".");
            return;
        }

        document_ = std::move(doc);
        current_book_ = store_.find_by_id(book.id);
        if (current_book_ == nullptr) {
            current_book_ = &book;
        }

        const documents::ReadingState state = store_.load_reading_state(book.id);
        section_index_ = state.section_index;
        if (section_index_ < 0 ||
            section_index_ >= static_cast<int>(document_->sections().size())) {
            section_index_ = 0;
        }

        phase_ = Phase::Reading;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Reading " + book.title +
                           (book.author.empty() ? "" : " by " + book.author) + ".");
        announce_section(ctx);
    }

    void handle_audio_playing(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            // Leave the playing screen but keep audio going (quick settings still control it).
            // Must speak over media — normal TTS is muted while audiobooks play.
            held_skip_.reset();
            phase_ = Phase::LocalList;
            rebuild_browse();
            sync_chrome(ctx);
            announce_over_media(
                ctx, "Local library. Playback continues. Hold Shift to hear speech while playing. "
                     "Menu for pause or stop. Backspace for Library menu.");
            return;
        }
        if (ctx.connect == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            const std::string response = ctx.connect->request("music.pause");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                const bool paused = braillatron::connect::json_get_bool(response, "paused", false);
                if (ctx.output != nullptr) {
                    ctx.output->set_media_paused(paused);
                }
                announce_over_media(ctx, paused ? "Paused" : "Playing");
            }
            return;
        }
    }

    void announce_local_book(UiContext &ctx)
    {
        const documents::LibraryBook &book = local_books_[browse_.focus_index()];
        announce_browse_item(ctx, book.title + (book.author.empty() ? "" : " by " + book.author));
    }

    void announce_search_result(UiContext &ctx)
    {
        const SearchResult &result = search_results_[browse_.focus_index()];
        announce_browse_item(ctx, result.title + " by " + result.author);
    }

    void announce_section(UiContext &ctx)
    {
        if (document_ == nullptr) {
            return;
        }
        const auto &sections = document_->sections();
        if (sections.empty()) {
            announce(ctx, "No readable sections.");
            return;
        }
        const documents::BookSection &section = sections[static_cast<size_t>(section_index_)];
        announce_browse_item(ctx, "Section " + section.title);
    }

    void read_section_text(UiContext &ctx)
    {
        if (document_ == nullptr) {
            return;
        }
        const auto &sections = document_->sections();
        if (section_index_ < 0 || section_index_ >= static_cast<int>(sections.size())) {
            return;
        }
        const documents::BookSection &section = sections[static_cast<size_t>(section_index_)];
        section_title_ = section.title.empty() ? "Section" : section.title;
        section_lines_ = wrap_text_lines(section.text);
        if (section_lines_.empty() && !section.text.empty()) {
            section_lines_.push_back(section.text);
        }
        if (section_lines_.empty()) {
            announce(ctx, "Empty section.");
            return;
        }
        section_line_index_ = 0;
        phase_ = Phase::ReadingText;
        rebuild_browse();
        sync_chrome(ctx);
        announce_section_line(ctx);
        save_reading_progress();
    }

    void handle_reading_text(keyboard::ControlKey key, UiContext &ctx)
    {
        if (section_lines_.empty()) {
            phase_ = Phase::Reading;
            rebuild_browse();
            sync_chrome(ctx);
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Reading;
            section_lines_.clear();
            section_line_index_ = 0;
            section_title_.clear();
            rebuild_browse();
            sync_chrome(ctx);
            announce_section(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && section_line_index_ > 0) {
            --section_line_index_;
            browse_.set_focus(section_line_index_);
            sync_chrome(ctx);
            announce_section_line(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            section_line_index_ + 1 < section_lines_.size()) {
            ++section_line_index_;
            browse_.set_focus(section_line_index_);
            sync_chrome(ctx);
            announce_section_line(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, true);
            return;
        }
        if (key == keyboard::ControlKey::Enter && ctx.motion != nullptr && ctx.braille != nullptr) {
            ctx.motion->emboss_text(section_lines_[section_line_index_], *ctx.braille);
            announce(ctx, "Embossing line");
        }
    }

    void handle_print_scope(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = phase_before_print_;
            rebuild_browse();
            sync_chrome(ctx);
            if (phase_ == Phase::ReadingText) {
                announce_section_line(ctx);
            } else {
                announce_section(ctx);
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
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        switch (browse_.focus_index()) {
        case static_cast<size_t>(PrintScopeChoice::CurrentPage):
            emboss_current_page(ctx);
            break;
        case static_cast<size_t>(PrintScopeChoice::ThisSection):
            emboss_current_section(ctx);
            break;
        case static_cast<size_t>(PrintScopeChoice::WholeBook):
            emboss_whole_book(ctx);
            break;
        default:
            break;
        }
        phase_ = phase_before_print_;
        rebuild_browse();
        sync_chrome(ctx);
    }

    void emboss_current_page(UiContext &ctx)
    {
        if (ctx.motion == nullptr || ctx.braille == nullptr || document_ == nullptr) {
            announce(ctx, "Embossing not available");
            return;
        }
        if (phase_before_print_ == Phase::ReadingText && !section_lines_.empty()) {
            ctx.motion->emboss_text(section_lines_[section_line_index_], *ctx.braille);
            announce(ctx, "Printing page");
            return;
        }
        const auto &sections = document_->sections();
        if (section_index_ < 0 || section_index_ >= static_cast<int>(sections.size())) {
            announce(ctx, "Nothing to print");
            return;
        }
        const std::vector<std::string> lines =
            wrap_text_lines(sections[static_cast<size_t>(section_index_)].text);
        if (lines.empty()) {
            announce(ctx, "Nothing to print");
            return;
        }
        ctx.motion->emboss_text(lines.front(), *ctx.braille);
        announce(ctx, "Printing page");
    }

    void emboss_current_section(UiContext &ctx)
    {
        if (ctx.motion == nullptr || ctx.braille == nullptr || document_ == nullptr) {
            announce(ctx, "Embossing not available");
            return;
        }
        const auto &sections = document_->sections();
        if (section_index_ < 0 || section_index_ >= static_cast<int>(sections.size())) {
            announce(ctx, "Nothing to print");
            return;
        }
        const std::string &text = sections[static_cast<size_t>(section_index_)].text;
        if (text.empty()) {
            announce(ctx, "Empty section");
            return;
        }
        ctx.motion->emboss_text(text, *ctx.braille);
        announce(ctx, "Printing section");
    }

    void emboss_whole_book(UiContext &ctx)
    {
        if (ctx.motion == nullptr || ctx.braille == nullptr || document_ == nullptr) {
            announce(ctx, "Embossing not available");
            return;
        }
        std::string text;
        for (const auto &section : document_->sections()) {
            if (!section.title.empty()) {
                if (!text.empty()) {
                    text += "\n\n";
                }
                text += section.title + "\n\n";
            }
            if (!section.text.empty()) {
                if (!text.empty() && section.title.empty()) {
                    text += "\n\n";
                }
                text += section.text;
            }
        }
        if (text.empty()) {
            announce(ctx, "Nothing to print");
            return;
        }
        ctx.motion->emboss_text(text, *ctx.braille);
        announce(ctx, "Printing book");
    }

    void announce_section_line(UiContext &ctx)
    {
        if (section_lines_.empty()) {
            return;
        }
        const std::string prefix = "Line " + std::to_string(section_line_index_ + 1) + " of " +
                                   std::to_string(section_lines_.size()) + ". ";
        announce(ctx, prefix + truncate_for_tts(section_lines_[section_line_index_]));
    }

    void save_reading_progress()
    {
        if (current_book_ == nullptr) {
            return;
        }
        documents::ReadingState state;
        state.book_id = current_book_->id;
        state.section_index = section_index_;
        state.char_offset = 0;
        state.updated_at = static_cast<uint64_t>(std::time(nullptr));
        store_.save_reading_state(state);
    }

    documents::LibraryStoreConfig config_;
    documents::LibraryStore store_;
    Phase phase_ = Phase::Menu;
    LayeredBrowseList browse_;
    std::string breadcrumb_;
    std::vector<documents::LibraryBook> local_books_;
    std::vector<SearchResult> search_results_;
    std::vector<WorthwhileResult> worthwhile_results_;
    std::string query_buffer_;
    std::string worthwhile_query_;
    std::string worthwhile_kind_;
    bool worthwhile_mode_recent_ = false;
    std::string active_source_;
    std::string active_source_label_;
    std::string pending_announce_;
    bool pending_refresh_local_ = false;
    const documents::LibraryBook *current_book_ = nullptr;
    std::unique_ptr<documents::EbookDocument> document_;
    int section_index_ = 0;
    std::vector<std::string> section_lines_;
    size_t section_line_index_ = 0;
    std::string section_title_;
    Phase phase_before_print_ = Phase::Reading;
    Phase phase_before_pick_ = Phase::LocalList;
    std::string now_playing_audio_;
    HeldAudioSkip held_skip_;
    size_t pending_remove_index_ = 0;
    size_t pending_rename_index_ = 0;
    std::string rename_buffer_;
    std::vector<std::string> usb_mounts_;
    std::vector<UsbEntry> usb_entries_;
    std::string usb_browse_path_;
};

} // namespace

std::unique_ptr<AppSession> make_library_app()
{
    return std::make_unique<LibraryApp>();
}

} // namespace braillatron::ui
