# Embosser firmware

Host-testable C pipeline for the cheaper Braillatron embosser. It streams a North American BRF job, splits each cell into Row A and Row B, and publishes `BRFSTAT` lines. The working motor section is an ESP32-S3 plus eight TMC2209 stepsticks on a passive carrier (see the two-product draft). There is no pin map, no ESP-IDF port, and no step generator here yet. Motor output is a struct of function pointers (`strike`, `feed_y`, `set_enable`).

The wire format is the cable grammar in [`shared/print_contract.h`](../shared/print_contract.h): 115200 8N1, optional first line `BRF1 filename.brf`, form feed (`0x0C`) or a caller-side 1500 ms idle gap (`emboss_pipeline_finish`) to close the job. Status is one line, `BRFSTAT <state>` or `BRFSTAT fault <reason>`. The pipeline does not keep a whole document in RAM.

`queued` is in the contract for a later spool-then-print path. This streaming pipeline goes idle, receiving, printing, then done or fault.

From this directory, or from the repository root (after the daemon check):

```bash
make check
```

That builds `emboss_pipeline_test` with `gcc -std=c11 -Wall -Wextra -Werror` and the shared header.
