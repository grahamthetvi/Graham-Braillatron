# Embosser firmware

Host-testable C for the cheaper Braillatron embosser. It streams a North American BRF job, splits each cell into Row A and Row B, and publishes `BRFSTAT` lines. The working motor section is a FYSETC Spider for the steps and one ESP32-S3 for Wi-Fi and USB (see the two-product draft). `emboss_steps` turns those moves into socket step counts. There is no step ISR and no ESP-IDF port here yet. Motor output is a struct of function pointers (`strike`, `travel_x`, `feed_y`, `set_enable`).

A sender still uses the cable grammar in [`shared/print_contract.h`](../shared/print_contract.h): 115200 8N1, optional first line `BRF1 filename.brf`, form feed (`0x0C`) or a caller-side 1500 ms idle gap to close the job. Status toward that sender is one line, `BRFSTAT <state>` or `BRFSTAT fault <reason>`. The pipeline does not keep a whole document in RAM.

The ESP32-S3 and the Spider share a different frame, [`shared/embosser_link.h`](../shared/embosser_link.h). Sync is `0xA6`. The ESP32 sends heartbeat, cell, newline, form feed, finish, and clear-fault. The Spider answers with a status opcode (job state, fault reason). Motors stay off until a heartbeat, and a 1000 ms gap is a safety cut. `emboss_spider_stall` is the same cut when the step loop misses its deadline. `emboss_radio` is the ESP32 side. `emboss_spider` is the Spider side. Both run on the host in `emboss_link_test`.

[`emboss_steps.h`](emboss_steps.h) is the socket list: 0 = X, 1 = Y, 2–7 = `emboss_1` … `emboss_6`. A 6.0 mm cell on a 40 mm/rev X axis is 480 microsteps. Row A fires before that move; Row B fires after the first 2.5 mm (200 steps). A punch is 2.0 mm out and the same distance back. `emboss_pipeline_set_paper_present(0)` refuses the next cell with `fault paper`. Until that call, no paper sensor is fitted. `emboss_steps_test` covers this.

`queued` is in the contract for a later spool-then-print path. This streaming pipeline goes idle, receiving, printing, then done or fault. There is no flash spool and no feed or cancel key.

From this directory, or from the repository root (after the daemon check):

```bash
make check
```

That builds `emboss_pipeline_test`, `emboss_link_test`, and `emboss_steps_test` with `gcc -std=c11 -Wall -Wextra -Werror` and the shared headers.
