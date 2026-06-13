#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <cctype>
#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

class MorseLearnApp final : public AppSession {
public:
    std::string id() const override { return "morse_learn"; }
    std::string label() const override { return "Morse Learning"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        lesson_index_ = 0;
        announce(ctx, "Morse lesson. Letter " + std::string(1, kAlphabet[lesson_index_]));
        if (ctx.output != nullptr) {
            ctx.output->play_morse(std::string(1, kAlphabet[lesson_index_]));
        }
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Morse lesson closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        const char expected = kAlphabet[lesson_index_];
        if (text[0] == expected || text[0] == static_cast<char>(std::toupper(text[0]))) {
            announce(ctx, "Correct");
            lesson_index_ = (lesson_index_ + 1) % (sizeof(kAlphabet) - 1);
            announce(ctx, "Next: " + std::string(1, kAlphabet[lesson_index_]));
            if (ctx.output != nullptr) {
                ctx.output->play_morse(std::string(1, kAlphabet[lesson_index_]));
            }
        } else {
            announce(ctx, "Try again");
        }
    }

    void on_control(keyboard::ControlKey, bool, UiContext &) override {}

private:
    static constexpr const char *kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t lesson_index_ = 0;
};

} // namespace

std::unique_ptr<AppSession> make_morse_learn_app()
{
    return std::make_unique<MorseLearnApp>();
}

} // namespace braillatron::ui
