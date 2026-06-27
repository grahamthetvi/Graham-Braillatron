#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../../documents/library_store.h"
#include "../layered_browse_list.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

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

enum class Phase {
    Menu,
    PublicSources,
    LocalList,
    SearchQuery,
    SearchResults,
    Downloading,
    Reading,
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
    InternetArchive,
    LibriVox,
};

enum class WorthwhileMenuChoice {
    SearchMovies,
    SearchShows,
    RecentMovies,
    RecentShows,
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

    void on_poll(UiContext &ctx) override
    {
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

    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override
    {
        return phase_ == Phase::SearchQuery || phase_ == Phase::WorthwhileSearch;
    }

    bool browse_list_active() const override
    {
        return phase_ == Phase::Menu || phase_ == Phase::PublicSources || phase_ == Phase::LocalList ||
               phase_ == Phase::SearchResults || phase_ == Phase::Reading ||
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
        return {};
    }

    std::vector<std::string> browse_items() const override { return browse_.labels(); }

    size_t browse_focus_index() const override { return browse_.focus_index(); }

    std::string browse_breadcrumb() const override { return breadcrumb_; }

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
        if (phase_ != Phase::SearchQuery && phase_ != Phase::WorthwhileSearch) {
            return;
        }
        if (phase_ == Phase::SearchQuery) {
            query_buffer_ += text;
        } else {
            worthwhile_query_ += text;
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
            items = {"Project Gutenberg", "Open Library", "Internet Archive", "LibriVox"};
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

    static std::string format_book_label(const documents::LibraryBook &book)
    {
        if (book.author.empty()) {
            return book.title;
        }
        return book.title + " - " + book.author;
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
            announce(ctx,
                     "Public sources. Project Gutenberg, Open Library, Internet Archive, LibriVox.");
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
        case static_cast<size_t>(PublicSourceChoice::InternetArchive):
            begin_source_search(ctx, "archive", "Internet Archive");
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
                    pending_announce_ = "Downloaded " + result.title + ". Open Music to play.";
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
        const std::string &text = sections[static_cast<size_t>(section_index_)].text;
        constexpr size_t kMaxAnnounce = 500;
        const std::string excerpt =
            text.size() > kMaxAnnounce ? text.substr(0, kMaxAnnounce) + "..." : text;
        announce(ctx, excerpt.empty() ? "Empty section." : excerpt);
        save_reading_progress();
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
};

} // namespace

std::unique_ptr<AppSession> make_library_app()
{
    return std::make_unique<LibraryApp>();
}

} // namespace braillatron::ui
