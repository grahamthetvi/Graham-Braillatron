# Print contract

Source of truth: [`print_contract.h`](print_contract.h). The Pi daemon and the embosser pipeline both include that header. This note is the short form.

## Geometry

| Quantity | Value |
| --- | --- |
| Cell pitch | 6.0 mm (6000 µm) |
| Row B offset | 2.5 mm (2500 µm) along the carriage |
| Line advance | 10 mm (10000 µm) |
| Page length | 33 lines, then a form feed starts a fresh page |
| Row A | dots 1, 3, 5 (`emboss_1`, `emboss_3`, `emboss_5`), bits 0, 2, 4, at the current X |
| Row B | dots 2, 4, 6 (`emboss_2`, `emboss_4`, `emboss_6`), bits 1, 3, 5, after the offset |

`emboss_1` … `emboss_6` are dots 1 … 6. Stepper gearing stays in each brain. The contract is millimetres and cells.

## BRF wire

A job is North American Braille ASCII. The 64-byte table (ASCII `0x20`..`0x5F`) is in the header.

- 115200 8N1
- Optional first line: `BRF1 filename.brf`
- Form feed (`0x0C`) ends the job, or 1500 ms of silence
- A space is a blank cell and still takes a cell width
- The Pi cable parser may store up to 2 MB. The embosser streams and must not require that buffer

## Status line

`BRFSTAT <state>\n`

When the state is `fault`: `BRFSTAT fault <reason>\n`. No reason token on any other state.

States: `idle`, `receiving`, `queued`, `printing`, `done`, `fault`.

Reasons: `none`, `safety`, `paper`, `motion`, `overflow`.

In C those reasons are `BRAILLATRON_PRINT_FAULT_*`. The co-processor link in `protocol.h` already uses `BRAILLATRON_FAULT_*` for freefall and comms loss, and the two enums have to keep different names.

A drop cuts motor power (`safety`) on either product. The personal-computer co-processor link is a different file, [`protocol.h`](protocol.h).
