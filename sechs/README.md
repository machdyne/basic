# Sechs core

The I2C target protocol of a Sechs module (see the
[Sechs specification](../docs/sechs.md)), independent of
the hardware.

- `sechs.h`, `sechs.c`: registers, broadcasts, address changes, INFO, the
  I2C console buffers.
- `test/sechs_test.c`: host tests that drive the core over a simulated I2C
  bus together with the interpreter and filesystem (`make test`).

## Using it on a target

- The I2C interrupt handler calls `sechs_start()` when addressed, `sechs_rx()` for each byte received, `sechs_tx()` for
  each byte to send, and `sechs_stop(unsent)` at the end of a transfer.
  `unsent` is the number of bytes `sechs_tx()` produced that were never
  sent: 1 at the end of a read on peripherals that load the next byte
  early (CH32V003, STM32), otherwise 0.
- The main loop reads console input with `sechs_getc()`, writes console
  output with `sechs_putc()`, acts on `sechs.cmd` (HALT, RUN, RESET), and
  keeps `sechs.r[SR_STATUS]`, `sechs.r[SR_OK]` and
  `sechs.r[SR_FAULT]` up to date.
- The target provides `sechs_set_addr()` (hardware address and persistent
  storage), `sechs_info()` (the INFO text) and `sechs_regs()` (the 16
  program registers).

`targets/ls10/ls10.c` is the reference: hardware I2C on PC1/PC2 with general
call enabled, a boot window, and the two consoles.

## Registers (implemented)

| Reg | Name | |
|---|---|---|
| 0x00-0x01 | SIG | `S6` |
| 0x02 | VER | 0x05 (Sechs 0.5) |
| 0x03 | CAPS | |
| 0x04 | STATUS | |
| 0x05 | OK | |
| 0x06 | FAULT | |
| 0x07 | CONTROL | 1 HALT, 2 RUN, 3 RESET |
| 0x08 | ADDR | new address, then its complement |
| 0x09 | INFO | `fw`, `mod`, `lang` |
| 0x18 | CIN | free space for console input |
| 0x19 | COUT | console output waiting |
| 0x1A | CDATA | console input (write) and output (read) |
| 0x80-0x8F | REG | program registers |

Not implemented, to fit the CH32V003 (all optional in Sechs 0.4):
broadcasts, HOLD, IDENTIFY, `pins` in INFO. Modules that share one address
all receive the same writes, which covers programming a batch.
