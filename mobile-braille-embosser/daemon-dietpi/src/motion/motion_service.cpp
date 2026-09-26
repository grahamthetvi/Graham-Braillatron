#include "motion_service.h"

#include "../documents/brf_format.h"
#include "../documents/liblouis_bridge.h"
#include "../motion_gate.h"

#include <iostream>

namespace braillatron::motion {

MotionService::MotionService(kinematics::KinematicsConfig config)
    : controller_(std::move(config))
{
    controller_.set_row_handlers(
        [this](uint8_t pin_mask, int64_t travel) {
            if (strike_logger_) {
                strike_logger_(pin_mask, travel);
            } else {
                std::cerr << "[motion] row strike mask=0x" << std::hex
                          << static_cast<unsigned>(pin_mask) << std::dec
                          << " travel=" << travel << "\n";
            }
        },
        [this](uint8_t pin_mask, int64_t travel) {
            if (strike_logger_) {
                strike_logger_(pin_mask, travel);
            } else {
                std::cerr << "[motion] row B strike mask=0x" << std::hex
                          << static_cast<unsigned>(pin_mask) << std::dec
                          << " travel=" << travel << "\n";
            }
        });
}

kinematics::MotionController &MotionService::controller()
{
    return controller_;
}

const kinematics::MotionController &MotionService::controller() const
{
    return controller_;
}

kinematics::PaperPosition &MotionService::paper()
{
    return paper_;
}

const kinematics::PaperPosition &MotionService::paper() const
{
    return paper_;
}

void MotionService::emboss_dot_mask(uint8_t dot_mask)
{
    if (braillatron::MotionGate::is_blocked()) {
        return;
    }
    controller_.emboss(dot_mask);
    // One chord = one cell: advance a full cell pitch, which also carries the
    // carriage past the Row B offset so deferred dots 2/4/6 fire in place.
    controller_.log_carriage_microsteps(
        static_cast<int32_t>(kinematics::MICROSTEPS_PER_CELL));
}

void MotionService::emboss_text(const std::string &plain,
                                const documents::BrailleTranslationService &braille)
{
    if (braillatron::MotionGate::is_blocked()) {
        return;
    }
    const std::string translated = braille.translate_forward(plain);
    for (unsigned char ch : translated) {
        const uint8_t mask = documents::braille_char_to_dot_mask(static_cast<wchar_t>(ch));
        if (mask != 0) {
            controller_.emboss(mask);
        }
        // Advance one cell pitch for every cell, including spaces and
        // unmapped characters, so blank cells keep their width.
        controller_.log_carriage_microsteps(
            static_cast<int32_t>(kinematics::MICROSTEPS_PER_CELL));
    }
}

void MotionService::emboss_brf(const std::string &brf)
{
    if (braillatron::MotionGate::is_blocked()) {
        return;
    }

    // Same fresh-page length as paper_separator (100 lb cardstock, 33 lines).
    constexpr int32_t kPageLines = 33;
    bool line_open = false;
    for (const documents::BrfToken &token : documents::tokenize_brf(brf)) {
        if (braillatron::MotionGate::is_blocked()) {
            return;
        }
        switch (token.mark) {
        case documents::BrfMark::Cell:
            emboss_dot_mask(token.dot_mask);
            line_open = true;
            break;
        case documents::BrfMark::Newline:
            advance_line();
            line_open = false;
            break;
        case documents::BrfMark::FormFeed:
            if (line_open) {
                advance_line();
                line_open = false;
            }
            {
                const int32_t into = paper_.y_line_index() % kPageLines;
                const int32_t feed = (into == 0) ? kPageLines : (kPageLines - into);
                feed_lines(feed);
            }
            break;
        }
    }
}

void MotionService::advance_line()
{
    if (braillatron::MotionGate::is_blocked()) {
        return false;
    }

    // Flush any deferred Row B strikes so the last cells of this line are
    // completed before the paper moves.
    controller_.log_carriage_microsteps(
        static_cast<int32_t>(controller_.row_b_deferral_microsteps()));

    // Physical Y feed plus carriage return to X0 (KlipperMotionBridge).
    if (line_feed_ && !line_feed_(1)) {
        std::cerr << "[motion] line feed failed; paper index left unchanged\n";
        return false;
    }

    // New line starts with the carriage at X0.
    controller_.reset_position(0);
    paper_.advance_line();
    return true;
}

bool MotionService::feed_lines(int32_t delta)
{
    if (braillatron::MotionGate::is_blocked()) {
        return false;
    }
    if (delta > 0) {
        for (int32_t i = 0; i < delta; ++i) {
            if (!advance_line()) {
                return false;
            }
        }
        return true;
    }
    if (delta < 0) {
        // Paper retreat is a pure Y move; the carriage travel log is X-only.
        if (line_feed_ && !line_feed_(delta)) {
            std::cerr << "[motion] line feed failed; paper index left unchanged\n";
            return false;
        }
        for (int32_t i = 0; i > delta; --i) {
            paper_.retreat_line();
        }
        return true;
    }
    return true;
}

void MotionService::reset_from_coordinate(int64_t x_microsteps, int32_t y_line_index)
{
    controller_.reset_position(x_microsteps);
    paper_.set_y_line_index(y_line_index);
}

void MotionService::set_row_strike_log(std::function<void(uint8_t, int64_t)> logger)
{
    strike_logger_ = std::move(logger);
}

void MotionService::set_line_feed_handler(std::function<bool(int32_t)> handler)
{
    line_feed_ = std::move(handler);
}

} // namespace braillatron::motion
