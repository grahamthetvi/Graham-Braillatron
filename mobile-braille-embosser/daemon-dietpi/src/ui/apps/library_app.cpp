#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../../documents/library_store.h"
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
    int gutenberg_id = 0;
    std::string title;
    std::string author;
};

enum class Phase {
    Menu,
    LocalList,
    SearchQuery,
    SearchResults,
    Downloading,
    Reading,
};

enum class MenuChoice {
    LocalLibrary,
    SearchPublicDomain,
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
        menu_index_ = 0;
        announce(ctx, "Library. Local books or search public domain.");
        announce_menu(ctx);
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
                local_index_ = 0;
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
        }
    }

    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override { return phase_ == Phase::SearchQuery; }

    void on_text(const std::string &text, UiContext &) override
    {
        if (phase_ != Phase::SearchQuery || text.empty()) {
            return;
        }
        query_buffer_ += text;
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
                phase_ = Phase::Menu;
                announce_menu(ctx);
            }
            break;
        case Phase::Reading:
            handle_reading(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Menu;
        menu_index_ = 0;
        local_books_.clear();
        local_index_ = 0;
        search_results_.clear();
        search_index_ = 0;
        query_buffer_.clear();
        pending_announce_.clear();
        pending_refresh_local_ = false;
        current_book_ = nullptr;
        document_.reset();
        section_index_ = 0;
    }

    void announce_menu(UiContext &ctx)
    {
        const std::string choice =
            menu_index_ == 0 ? "Local library" : "Search public domain";
        announce(ctx, choice + ". Press Enter to select.");
    }

    void handle_menu(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::DpadUp && menu_index_ > 0) {
            --menu_index_;
            announce_menu(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && menu_index_ < 1) {
            ++menu_index_;
            announce_menu(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (menu_index_ == static_cast<int>(MenuChoice::LocalLibrary)) {
            local_books_ = store_.books();
            if (local_books_.empty()) {
                announce(ctx, "No local books. Search public domain or import EPUB via LocalSend.");
                return;
            }
            phase_ = Phase::LocalList;
            local_index_ = 0;
            announce_local_book(ctx);
            return;
        }

        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable for public domain search.");
            return;
        }
        phase_ = Phase::SearchQuery;
        query_buffer_.clear();
        announce(ctx, "Search public domain. Type title or author and press Enter.");
    }

    void handle_local_list(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Menu;
            announce_menu(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && local_index_ > 0) {
            --local_index_;
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && local_index_ + 1 < local_books_.size()) {
            ++local_index_;
            announce_local_book(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || local_books_.empty()) {
            return;
        }
        open_book(ctx, local_books_[local_index_]);
    }

    void handle_search_query(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                return;
            }
            phase_ = Phase::Menu;
            announce_menu(ctx);
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
        search_index_ = 0;
        announce(ctx, "Searching for " + query_buffer_ + ".");

        ctx.connect->request_async(
            "library.search", "\"query\":\"" + braillatron::connect::json_escape(query_buffer_) + "\"",
            [this](const std::string &response) { parse_search_response(response); });
    }

    void handle_search_results(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::SearchQuery;
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (search_results_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && search_index_ > 0) {
            --search_index_;
            announce_search_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && search_index_ + 1 < search_results_.size()) {
            ++search_index_;
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

        const SearchResult &result = search_results_[search_index_];
        phase_ = Phase::Downloading;
        announce(ctx, "Downloading " + result.title + ".");

        ctx.connect->request_async(
            "library.download",
            "\"gutenberg_id\":\"" + std::to_string(result.gutenberg_id) + "\"",
            [this, result](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    pending_refresh_local_ = true;
                    pending_announce_ = "Downloaded " + result.title + ".";
                } else {
                    pending_announce_ = "Download failed for " + result.title + ".";
                    phase_ = Phase::SearchResults;
                }
            });
    }

    void handle_reading(keyboard::ControlKey key, UiContext &ctx)
    {
        if (document_ == nullptr || document_->sections().empty()) {
            phase_ = Phase::LocalList;
            return;
        }

        const auto &sections = document_->sections();
        if (key == keyboard::ControlKey::Backspace) {
            save_reading_progress();
            phase_ = Phase::LocalList;
            announce_local_book(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && section_index_ > 0) {
            --section_index_;
            announce_section(ctx);
            save_reading_progress();
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            section_index_ + 1 < static_cast<int>(sections.size())) {
            ++section_index_;
            announce_section(ctx);
            save_reading_progress();
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
            return;
        }

        const std::string body = braillatron::connect::json_get_array_body(response, "results");
        for (const auto &obj : braillatron::connect::json_split_objects("[" + body + "]")) {
            SearchResult result;
            const std::string id = braillatron::connect::json_get_string(obj, "id");
            result.gutenberg_id = id.empty() ? 0 : std::stoi(id);
            result.title = braillatron::connect::json_get_string(obj, "title");
            result.author = braillatron::connect::json_get_string(obj, "author");
            if (result.gutenberg_id > 0 && !result.title.empty()) {
                search_results_.push_back(std::move(result));
            }
        }

        if (search_results_.empty()) {
            pending_announce_ = "No results for " + query_buffer_ + ".";
            phase_ = Phase::SearchQuery;
            return;
        }

        search_index_ = 0;
        phase_ = Phase::SearchResults;
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
        announce(ctx, "Reading " + book.title +
                           (book.author.empty() ? "" : " by " + book.author) + ".");
        announce_section(ctx);
    }

    void announce_local_book(UiContext &ctx)
    {
        const documents::LibraryBook &book = local_books_[local_index_];
        announce(ctx, "Book " + std::to_string(local_index_ + 1) + " of " +
                           std::to_string(local_books_.size()) + ". " + book.title +
                           (book.author.empty() ? "" : " by " + book.author));
    }

    void announce_search_result(UiContext &ctx)
    {
        const SearchResult &result = search_results_[search_index_];
        announce(ctx, "Result " + std::to_string(search_index_ + 1) + " of " +
                           std::to_string(search_results_.size()) + ". " + result.title + " by " +
                           result.author);
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
        announce(ctx, "Section " + std::to_string(section_index_ + 1) + " of " +
                           std::to_string(sections.size()) + ". " + section.title + ".");
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
    int menu_index_ = 0;
    std::vector<documents::LibraryBook> local_books_;
    size_t local_index_ = 0;
    std::vector<SearchResult> search_results_;
    size_t search_index_ = 0;
    std::string query_buffer_;
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
