#pragma once

#include <cstdint>

namespace braillatron::kinematics {

/*
 * Derived from .cursorrules §3.3 — Y-axis tractor feed kinematics proof:
 *   C = 10 * 12.7 mm = 127 mm
 *   R = Z2/Z1 = 127/20 = 6.35
 *   Linear travel per motor revolution = C/R = 20 mm
 *   Resolution per full step = 20 mm / 200 = 0.1 mm/step
 *   10 mm line advance = 100 full steps = 1,600 microsteps @ 16x
 */

constexpr double PITCH_CIRCUMFERENCE_MM = 127.0;
constexpr double DRIVE_PULLEY_TEETH = 20.0;
constexpr double DRIVEN_PULLEY_TEETH = 127.0;
constexpr double REDUCTION_RATIO = DRIVEN_PULLEY_TEETH / DRIVE_PULLEY_TEETH; // 6.35

constexpr double TRAVEL_PER_MOTOR_REV_MM = PITCH_CIRCUMFERENCE_MM / REDUCTION_RATIO; // 20.0
constexpr uint32_t FULL_STEPS_PER_MOTOR_REV = 200u;
constexpr double MM_PER_FULL_STEP = TRAVEL_PER_MOTOR_REV_MM / static_cast<double>(FULL_STEPS_PER_MOTOR_REV);

constexpr uint32_t MICROSTEPS_PER_FULL_STEP = 16u;
constexpr double MM_PER_MICROSTEP = MM_PER_FULL_STEP / static_cast<double>(MICROSTEPS_PER_FULL_STEP);
constexpr uint32_t MICROSTEPS_PER_MOTOR_REV =
    FULL_STEPS_PER_MOTOR_REV * MICROSTEPS_PER_FULL_STEP; // 3200

constexpr double LINE_ADVANCE_MM = 10.0;
constexpr uint32_t FULL_STEPS_PER_10MM_LINE =
    static_cast<uint32_t>(LINE_ADVANCE_MM / MM_PER_FULL_STEP); // 100
constexpr uint32_t MICROSTEPS_PER_10MM_LINE =
    FULL_STEPS_PER_10MM_LINE * MICROSTEPS_PER_FULL_STEP; // 1600

/*
 * Assumed X carriage drive (matches klipper/printer.cfg stepper_x):
 *   GT2 2 mm-pitch belt, 20-tooth pulley → 40 mm per motor revolution.
 *   200 full steps/rev → 0.2 mm/full-step on X (distinct from Y's 0.1 mm).
 * Not CAD-verified; no pulley/belt PN is claimed. Commanded X millimetres
 * (cell pitch, Row B stagger) are physically correct only with this drive.
 */
constexpr double X_BELT_PITCH_MM = 2.0;
constexpr double X_PULLEY_TEETH = 20.0;
constexpr double X_ROTATION_DISTANCE_MM = X_BELT_PITCH_MM * X_PULLEY_TEETH; // 40.0
constexpr double X_MM_PER_FULL_STEP =
    X_ROTATION_DISTANCE_MM / static_cast<double>(FULL_STEPS_PER_MOTOR_REV); // 0.2

/*
 * Braille cell pitch (Marburg Medium: 2.5 mm dot spacing, 6.0 mm cell-to-cell,
 * 10 mm line spacing — consistent with ROW_B_X_OFFSET_MM and LINE_ADVANCE_MM).
 *
 * Software stores carriage X in the Y-derived 0.1 mm/full-step unit so
 * microsteps_to_mm() yields millimetres; Klipper then applies stepper_x
 * rotation_distance 40. Those millimetres match the page only if the GT2/20T
 * assumption above is the hardware that is fitted.
 */
constexpr double CELL_PITCH_MM = 6.0;
constexpr uint32_t FULL_STEPS_PER_CELL =
    static_cast<uint32_t>(CELL_PITCH_MM / MM_PER_FULL_STEP); // 60
constexpr uint32_t MICROSTEPS_PER_CELL =
    FULL_STEPS_PER_CELL * MICROSTEPS_PER_FULL_STEP; // 960

/*
 * .cursorrules §3.4 — Row A (dots 1,3,5) vs Row B (dots 2,4,6) staggered 2.5 mm
 * on X. Valid on the page only if printer.cfg X is GT2/20T (rotation_distance 40).
 */
constexpr double ROW_B_X_OFFSET_MM = 2.5;
constexpr uint32_t FULL_STEPS_ROW_B_OFFSET =
    static_cast<uint32_t>(ROW_B_X_OFFSET_MM / MM_PER_FULL_STEP); // 25
constexpr uint32_t MICROSTEPS_ROW_B_OFFSET =
    FULL_STEPS_ROW_B_OFFSET * MICROSTEPS_PER_FULL_STEP; // 400

constexpr uint8_t ROW_A_DOT_MASK = (1u << 0) | (1u << 2) | (1u << 4); // dots 1, 3, 5
constexpr uint8_t ROW_B_DOT_MASK = (1u << 1) | (1u << 3) | (1u << 5); // dots 2, 4, 6
constexpr uint8_t BRAILLE_DOT_MASK = ROW_A_DOT_MASK | ROW_B_DOT_MASK;

inline constexpr uint32_t mm_to_full_steps(double mm)
{
    return static_cast<uint32_t>(mm / MM_PER_FULL_STEP);
}

inline constexpr uint32_t mm_to_microsteps(double mm)
{
    return static_cast<uint32_t>(mm / MM_PER_MICROSTEP);
}

inline constexpr double microsteps_to_mm(int64_t microsteps)
{
    return static_cast<double>(microsteps) * MM_PER_MICROSTEP;
}

inline constexpr uint8_t row_a_pins(uint8_t dot_mask)
{
    return static_cast<uint8_t>(dot_mask & ROW_A_DOT_MASK);
}

inline constexpr uint8_t row_b_pins(uint8_t dot_mask)
{
    return static_cast<uint8_t>(dot_mask & ROW_B_DOT_MASK);
}

static_assert(REDUCTION_RATIO == 6.35, "reduction ratio must be 6.35");
static_assert(MM_PER_FULL_STEP == 0.1, "linear resolution must be 0.1 mm/step");
static_assert(FULL_STEPS_PER_10MM_LINE == 100u, "10 mm must equal 100 full steps");
static_assert(MICROSTEPS_PER_10MM_LINE == 1600u, "10 mm must equal 1600 microsteps at 16x");
static_assert(FULL_STEPS_ROW_B_OFFSET == 25u, "2.5 mm must equal 25 full steps");
static_assert(MICROSTEPS_ROW_B_OFFSET == 400u, "2.5 mm must equal 400 microsteps at 16x");
static_assert(FULL_STEPS_PER_CELL == 60u, "6.0 mm cell must equal 60 full steps");
static_assert(MICROSTEPS_PER_CELL == 960u, "6.0 mm cell must equal 960 microsteps at 16x");
static_assert(X_ROTATION_DISTANCE_MM == 40.0, "X GT2/20T must be 40 mm/rev");
static_assert(X_MM_PER_FULL_STEP == 0.2, "X GT2/20T must be 0.2 mm/full-step");

} // namespace braillatron::kinematics
