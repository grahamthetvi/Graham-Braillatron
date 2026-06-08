#include "fail_safes.h"

#include "pins.h"

#include <Arduino.h>

void fail_safes_init(void)
{
    pinMode(PIN_STEPPER_CUT, OUTPUT);
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}

void fail_safes_poll(void)
{
    /* Stepper rail is cut in INT0 ISR; no main-loop recovery in prototype. */
}
