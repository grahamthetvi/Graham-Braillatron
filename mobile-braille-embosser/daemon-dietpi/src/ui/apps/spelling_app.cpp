#include "../../documents/spelling_list_store.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class Phase {
    PickList,
    PickMode,
    Learn,
    Quiz,
    Review,
};

enum class SessionMode {
    Learn,
    Quiz,
    Review,
};

std::string normalize_word(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

std::string session_timestamp()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
                             .count();
    return std::to_string(seconds);
}

class SpellingApp final : public AppSession {
public:
    std::string id() const override { return "spelling"; }
    std::string label() const override { return "Spelling"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        store_.refresh();
        if (store_.lists().empty()) {
            announce(ctx, "No spelling lists available.");
            return;
        }
        const auto *default_list = store_.find_list(config_.default_list_id);
        list_index_ = 0;
        if (default_list != nullptr) {
            for (size_t i = 0; i < store_.lists().size(); ++i) {
                if (store_.lists()[i].id == default_list->id) {
                    list_index_ = i;
                    break;
                }
            }
        }
        phase_ = Phase::PickList;
        announce_list(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        save_session_if_needed();
        reset_session();
        announce(ctx, "Spelling closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override
    {
        return phase_ == Phase::Quiz || phase_ == Phase::Review;
    }

    void on_text(const std::string &text, UiContext &) override
    {
        if ((phase_ != Phase::Quiz && phase_ != Phase::Review) || text.empty()) {
            return;
        }
        answer_buffer_ += text;
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        switch (phase_) {
        case Phase::PickList:
            handle_pick_list(key, ctx);
            break;
        case Phase::PickMode:
            handle_pick_mode(key, ctx);
            break;
        case Phase::Learn:
            handle_learn(key, ctx);
            break;
        case Phase::Quiz:
        case Phase::Review:
            handle_quiz(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::PickList;
        mode_index_ = 0;
        list_index_ = 0;
        word_index_ = 0;
        answer_buffer_.clear();
        session_ = {};
        active_list_ = nullptr;
        review_words_.clear();
    }

    void save_session_if_needed()
    {
        if (session_.attempts == 0) {
            return;
        }
        store_.save_session(session_, session_timestamp());
    }

    void handle_pick_list(keyboard::ControlKey key, UiContext &ctx)
    {
        const auto &lists = store_.lists();
        if (lists.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            announce(ctx, "Spelling closed");
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && list_index_ > 0) {
            --list_index_;
            announce_list(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && list_index_ + 1 < lists.size()) {
            ++list_index_;
            announce_list(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        active_list_ = &lists[list_index_];
        session_.list_id = active_list_->id;
        phase_ = Phase::PickMode;
        announce_mode(ctx);
    }

    void handle_pick_mode(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::PickList;
            announce_list(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && mode_index_ > 0) {
            --mode_index_;
            announce_mode(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            mode_index_ + 1 < static_cast<int>(sizeof(kModes) / sizeof(kModes[0]))) {
            ++mode_index_;
            announce_mode(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || active_list_ == nullptr) {
            return;
        }

        word_index_ = 0;
        answer_buffer_.clear();
        session_.score = 0;
        session_.attempts = 0;
        session_.missed_words.clear();

        if (mode_index_ == 0) {
            phase_ = Phase::Learn;
            announce_learn_word(ctx);
            return;
        }
        if (mode_index_ == 1) {
            phase_ = Phase::Quiz;
            announce_quiz_word(ctx);
            return;
        }

        review_words_ = session_.missed_words;
        if (review_words_.empty() && active_list_ != nullptr) {
            review_words_ = active_list_->words;
        }
        if (review_words_.empty()) {
            announce(ctx, "No missed words to review.");
            phase_ = Phase::PickMode;
            announce_mode(ctx);
            return;
        }
        phase_ = Phase::Review;
        announce_quiz_word(ctx);
    }

    void handle_learn(keyboard::ControlKey key, UiContext &ctx)
    {
        if (active_list_ == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::PickMode;
            announce_mode(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && word_index_ > 0) {
            --word_index_;
            announce_learn_word(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown &&
            word_index_ + 1 < active_list_->words.size()) {
            ++word_index_;
            announce_learn_word(ctx);
        }
    }

    void handle_quiz(keyboard::ControlKey key, UiContext &ctx)
    {
        const std::vector<std::string> &words =
            phase_ == Phase::Review ? review_words_ : active_list_->words;
        if (words.empty()) {
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (!answer_buffer_.empty()) {
                answer_buffer_.pop_back();
                return;
            }
            phase_ = Phase::PickMode;
            announce_mode(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const std::string expected = normalize_word(words[word_index_]);
        const std::string actual = normalize_word(answer_buffer_);
        ++session_.attempts;
        if (actual == expected) {
            ++session_.score;
            announce(ctx, "Correct");
        } else {
            if (std::find(session_.missed_words.begin(), session_.missed_words.end(),
                          words[word_index_]) == session_.missed_words.end()) {
                session_.missed_words.push_back(words[word_index_]);
            }
            announce(ctx, "Incorrect. Correct spelling: " + words[word_index_]);
        }

        answer_buffer_.clear();
        if (word_index_ + 1 >= words.size()) {
            save_session_if_needed();
            announce(ctx, "Quiz finished. Score " + std::to_string(session_.score) + " of " +
                               std::to_string(session_.attempts));
            phase_ = Phase::PickMode;
            announce_mode(ctx);
            return;
        }

        ++word_index_;
        announce_quiz_word(ctx);
    }

    void announce_list(UiContext &ctx)
    {
        const auto &lists = store_.lists();
        if (list_index_ >= lists.size()) {
            return;
        }
        announce(ctx, "List " + std::to_string(list_index_ + 1) + " of " +
                           std::to_string(lists.size()) + ". " + lists[list_index_].name);
    }

    void announce_mode(UiContext &ctx)
    {
        announce(ctx, "Mode " + std::string(kModes[mode_index_]) + ". Enter to start.");
    }

    void announce_learn_word(UiContext &ctx)
    {
        if (active_list_ == nullptr || word_index_ >= active_list_->words.size()) {
            return;
        }
        announce(ctx, "Word " + std::to_string(word_index_ + 1) + " of " +
                           std::to_string(active_list_->words.size()) + ". " +
                           active_list_->words[word_index_]);
    }

    void announce_quiz_word(UiContext &ctx)
    {
        const std::vector<std::string> &words =
            phase_ == Phase::Review ? review_words_ : active_list_->words;
        if (word_index_ >= words.size()) {
            return;
        }
        announce(ctx, "Spell word " + std::to_string(word_index_ + 1) + " of " +
                           std::to_string(words.size()) + ".");
    }

    static constexpr const char *kModes[] = {"Learn", "Quiz", "Review missed"};
    documents::SpellingConfig config_ =
        documents::load_spelling_config("/etc/braillatron/spelling.conf");
    documents::SpellingListStore store_ {config_};
    Phase phase_ = Phase::PickList;
    size_t list_index_ = 0;
    size_t mode_index_ = 0;
    size_t word_index_ = 0;
    std::string answer_buffer_;
    documents::SpellingSessionState session_;
    const documents::SpellingList *active_list_ = nullptr;
    std::vector<std::string> review_words_;
};

} // namespace

std::unique_ptr<AppSession> make_spelling_app()
{
    return std::make_unique<SpellingApp>();
}

} // namespace braillatron::ui
