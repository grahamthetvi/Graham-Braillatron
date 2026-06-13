#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../documents/brf_store.h"
#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <cstdint>
#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

class TranscriberApp final : public AppSession {
public:
    std::string id() const override { return "transcriber"; }
    std::string label() const override { return "Transcriber"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        queue_depth_ = 0;
        motion_ = ctx.motion;
        brf_ = ctx.brf;
        output_ = ctx.output;
        braille_ = ctx.braille;
        if (output_ != nullptr) {
            output_->set_stt_transcript_handler(
                [this](const std::string &text, bool is_final) {
                    if (!is_final || text.empty()) {
                        return;
                    }
                    ++queue_depth_;
                    if (queue_depth_ > 8) {
                        if (output_ != nullptr) {
                            output_->announce_message("Buffer full. Please pause.");
                            output_->play_boundary_haptic();
                        }
                        return;
                    }
                    if (motion_ != nullptr && braille_ != nullptr) {
                        motion_->emboss_text(text, *braille_);
                    }
                    if (brf_ != nullptr) {
                        brf_->append_line(text);
                        brf_->save();
                    }
                    --queue_depth_;
                });
        }
        announce(ctx, "Transcriber listening. Hold speech button.");
    }

    void on_exit(UiContext &ctx) override
    {
        if (output_ != nullptr) {
            output_->set_stt_transcript_handler(nullptr);
        }
        motion_ = nullptr;
        brf_ = nullptr;
        output_ = nullptr;
        braille_ = nullptr;
        announce(ctx, "Transcriber closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}

private:
    uint32_t queue_depth_ = 0;
    motion::MotionService *motion_ = nullptr;
    documents::BrfStore *brf_ = nullptr;
    documents::BrailleTranslationService *braille_ = nullptr;
    OutputHub *output_ = nullptr;
};

} // namespace

std::unique_ptr<AppSession> make_transcriber_app()
{
    return std::make_unique<TranscriberApp>();
}

} // namespace braillatron::ui
