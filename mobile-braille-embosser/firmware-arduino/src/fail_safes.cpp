#include "fail_safes.h"

#include "pins.h"

#include <Arduino.h>

void fail_safes_init(void)
{
    pinMode(PIN_STEPPER_CUT, OUTPUT);
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}

void fail_safes_cut_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, LOW);
}

void fail_safes_restore_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}
