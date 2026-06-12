#include "motion_service.h"

#include "../documents/liblouis_bridge.h"

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
    controller_.emboss(dot_mask);
}

void MotionService::emboss_text(const std::string &plain,
                                const documents::BrailleTranslationService &braille)
{
    const std::string translated = braille.translate_forward(plain);
    for (unsigned char ch : translated) {
        const uint8_t mask = documents::braille_char_to_dot_mask(static_cast<wchar_t>(ch));
        if (mask != 0) {
            controller_.emboss(mask);
            controller_.log_carriage_microsteps(
                static_cast<int32_t>(kinematics::MICROSTEPS_PER_10MM_LINE / 40));
        }
    }
}

void MotionService::advance_line()
{
    controller_.advance_line_10mm();
    paper_.advance_line();
}

void MotionService::feed_lines(int32_t delta)
{
    if (delta > 0) {
        for (int32_t i = 0; i < delta; ++i) {
            advance_line();
        }
    } else if (delta < 0) {
        for (int32_t i = 0; i > delta; --i) {
            controller_.log_carriage_microsteps(
                -static_cast<int32_t>(kinematics::MICROSTEPS_PER_10MM_LINE));
            paper_.retreat_line();
        }
    }
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

} // namespace braillatron::motion
