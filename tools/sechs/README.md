# sechsctl

A [Sechs](../../docs/sechs.md) master for Linux. It
reaches modules in one of two ways:

- a Linux I2C bus (`/dev/i2c-N`, `-b N`), for example on a Raspberry Pi,
  with the module's pins 1 (SCL) and 2 (SDA) on the bus and pull-ups on
  both;
- a USB bridge (`-d DEV`): Werkzeug's second USB port, with a module in a
  Wolfszahn on its PMOD. The bridge also gives a terminal the module's UART
  console.

```
make sechsctl
./sechsctl scan                       # list modules on /dev/i2c-1
./sechsctl -b 3 scan                  # another bus
./sechsctl info 0x0c                  # identity, status, INFO
./sechsctl send 0x0c prog.txt         # type a file into the I2C console
./sechsctl console 0x0c               # interactive (end with Ctrl-D)
./sechsctl halt 0x0c                  # also: run, reset
./sechsctl addr 0x0c 0x21             # give the module a new address
./sechsctl reg 0x21 3                 # read program register 3 (REG 3)
./sechsctl reg 0x21 3 100             # write it
```

`send` types each line of the file into the module's console, waits for
the module to finish, prints what it answered, and exits with status 1 if
the last command failed (OK register). To load a program onto a module:

```
NEW
10 PINS NET,NET,OD,AIN
20 OUT 3, 1
SAVE BOOT
```

`sechsctl send 0x0c thatfile` then leaves it in `BOOT.BAS`, to be run at the
next power-on, and `echo "TYPE BOOT" | sechsctl send 0x0c` reads it back.

`make sechs_sim` builds the same tool talking to a simulated module (the
real interpreter, filesystem and Sechs core, with its F-RAM in
`ZZSIM.IMG`); the test suite uses it. Each run starts a fresh module (only
the F-RAM persists), so program registers do not carry over between runs.

## The Werkzeug bridge

The Werkzeug firmware (`targets/werkzeug`) has two USB serial ports: the
BASIC console and the Sechs bridge (interface name "Werkzeug Sechs bridge",
`/dev/serial/by-id/...-if02`). The bridge reaches a module in a Wolfszahn
on the PMOD socket (top row):

| Module pin | PMOD | Werkzeug | Use |
|---|---|---|---|
| 1 (A, SCL) | 1 | GPIO19 | I2C, about 50 kHz |
| 2 (B, SDA) | 2 | GPIO17 | |
| 3 (C) | 3 | GPIO15 | UART to the module (PIO) |
| 4 (D) | 4 | GPIO13 | UART from the module (UART0) |

I2C uses the RP2040's internal pull-ups; for long wires add 2.2-4.7k
pull-ups on pins 1 and 2.

```
./sechsctl -d /dev/serial/by-id/usb-Machdyne_Werkzeug_*-if02 scan
./sechsctl -d /dev/ttyACM1 send 0x0c prog.txt
./sechsctl -d /dev/ttyACM1 uart 115200    # the UART console; Enter wakes it
./sechsctl -d /dev/ttyACM1 flash ls10.bin # write a module's firmware
```

`flash` writes the firmware of an LS10 connected to Werkzeug's programming
wires (see [docs/ch32prog.md](../../docs/ch32prog.md) for the wiring and the
failsafe rules). The image is checked before the module is touched, and
the module's files are kept.

UART mode lasts until the port is closed. The protocol is plain text (see
`bridge.h`), so a terminal program works too: type `u 115200` and press
Enter. The bridge answers `busy` while a BASIC program on the Werkzeug has
declared the pins it needs.

The bridge protocol is tested on the host (`make test`): `bridge_host`
runs `bridge.c` on a pseudo-terminal with a simulated module behind it.

## Testing a module

`hwtest.sh` tests a module running Machdyne BASIC through `sechsctl`
(identity, consoles, files, registers with a running program, HALT,
faults, address changes, RESET with BOOT.BAS). It does not format storage
and keeps an existing BOOT.BAS:

```
tools/sechs/hwtest.sh -d /dev/ttyACM0
```

The test suite runs it against a simulated module. The full hardware test
plan, including what needs a meter or a sensor, is
[docs/hwtest.md](../../docs/hwtest.md).
