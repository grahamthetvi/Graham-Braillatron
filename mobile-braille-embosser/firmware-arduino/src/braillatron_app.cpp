#include "braillatron_app.h"

#include "fail_safes.h"
#include "keyboard_matrix.h"
#include "mpu6050_isr.h"
#include "protocol_tx.h"
#include "watchdog.h"

#include "protocol.h"

static bool g_safety_frame_sent = false;

void braillatron_setup(void)
{
    fail_safes_init();
    protocol_tx_init();
    mpu6050_isr_init();
    keyboard_matrix_init();
    watchdog_init();
}

void braillatron_loop(void)
{
    if (mpu6050_freefall_pending()) {
        if (!g_safety_frame_sent) {
            protocol_tx_safety(
                BRAILLATRON_FAULT_FREEFALL,
                BRAILLATRON_SEVERITY_LATCHED,
                0u);
            g_safety_frame_sent = true;
        }
        return;
    }

    keyboard_matrix_poll();
    watchdog_kick();
}
