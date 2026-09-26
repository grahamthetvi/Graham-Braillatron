# Embosser firmware

Host-testable C for the cheaper Braillatron embosser. It streams a North American BRF job, splits each cell into Row A and Row B, and publishes `BRFSTAT` lines. The working motor section is a FYSETC Spider for the steps and one ESP32-S3 for Wi-Fi and USB (see the two-product draft). There is no Spider step generator and no ESP-IDF port here yet. Motor output is a struct of function pointers (`strike`, `feed_y`, `set_enable`).

A sender still uses the cable grammar in [`shared/print_contract.h`](../shared/print_contract.h): 115200 8N1, optional first line `BRF1 filename.brf`, form feed (`0x0C`) or a caller-side 1500 ms idle gap to close the job. Status toward that sender is one line, `BRFSTAT <state>` or `BRFSTAT fault <reason>`. The pipeline does not keep a whole document in RAM.

The ESP32-S3 and the Spider share a different frame, [`shared/embosser_link.h`](../shared/embosser_link.h). Sync is `0xA6`. The ESP32 sends heartbeat, cell, newline, form feed, finish, and clear-fault. The Spider answers with a status opcode (job state, fault reason). Motors stay off until a heartbeat, and a 1000 ms gap is a safety cut. `emboss_radio` is the ESP32 side. `emboss_spider` is the Spider side. Both run on the host in `emboss_link_test`.

`queued` is in the contract for a later spool-then-print path. This streaming pipeline goes idle, receiving, printing, then done or fault.

From this directory, or from the repository root (after the daemon check):

```bash
make check
```

That builds `emboss_pipeline_test` and `emboss_link_test` with `gcc -std=c11 -Wall -Wextra -Werror` and the shared headers.
