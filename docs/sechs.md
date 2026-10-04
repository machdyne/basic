# Sechs

**Status: Draft 0.5 (2026-10-02). A first implementation exists (Machdyne
BASIC on the Zwölf LS10A), tested on the host; not yet on hardware. Not yet committed. Register numbers and
command codes are provisional until the first compliant firmware is built.**

Sechs (German for "six") is an interface standard for small, inexpensive,
long-lived programmable modules with six contacts: a host I2C bus, two local
I/O pins, ground and 3.3V. A module that follows this spec can be plugged into
any carrier that follows it, managed by any master that follows it, and
will not damage or disturb either.

The keywords MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as described
in RFC 2119.

## 1. What Sechs is

Sechs is a small contract between:

- **modules**, which run programs,
- **carriers**, which modules plug into and which give the local pins a
  meaning,
- **masters**, which talk to modules over I2C.

It defines the six-pin interface, how a module behaves from power-on until
its program runs, a minimal I2C protocol every module implements, and a few
optional profiles (files, consoles) that most modules will also
implement.

Compliance is enforced by the firmware. A user writing a program for a
compliant firmware cannot make the module violate this spec: a program that
would break a rule refuses to run.

## 2. What Sechs is not

- **Not a CPU, VM or instruction set.** Modules may be built from different
  MCUs, FPGAs or other devices, and may execute programs in completely
  different ways.
- **Not a programming language.** Languages are defined separately. The goal
  is that the same language (for example Machdyne BASIC) can be used on
  different modules, interpreted on some and compiled for others.
- **Not about installing firmware.** How firmware gets onto a module is out of
  scope. Sechs assumes a compliant firmware is present.
- **Not a package.** Only the six Sechs contacts are specified. Modules may
  have more pins (the 12-pin Zwölf package) or exactly six.
- **Not binding on other firmware.** Any firmware may be written for any
  module. Only firmware that claims Sechs compliance must follow this spec.

## 3. Design constraint: the smallest module

Sechs modules are meant to be cheap enough to buy by the box. The core of the
spec is therefore sized so that it can be implemented on very small devices:
an MCU with about 1K instruction words and 64 bytes of RAM, or a few hundred
FPGA logic cells. Everything that a small device could not reasonably do is
an optional profile.

## 4. Relationship to Zwölf

[Zwölf](https://github.com/machdyne/zwolf) defined 12-pin modules built
around a portable stack CPU. Most of its value turned out to be in the six-pin
interface on pins 1-6. Sechs keeps that interface unchanged and drops the
rest. A Zwölf module running compliant firmware is a Sechs module, but a Sechs
module need not be a Zwölf module. The zwolf repository is kept for
historical purposes.

## 5. Terms

| Term | Meaning |
|---|---|
| **Module** | The device with the six contacts. |
| **Firmware** | The module's built-in software (or gateware). Installed by means outside this spec. |
| **Program** | What a user loads into the module, in a language the firmware accepts. Stored as a file. |
| **File** | A named unit of storage on the module: a program or data (for example a log). |
| **Carrier** | A board with a Sechs socket. Powers the module and gives pins C/D a meaning. |
| **Master** | Anything that drives the I2C bus on A/B: it starts every transfer and drives the clock. |
| **Programmer** | A master (often with a terminal) used to load programs into modules. |
| **Networked / stand-alone** | Whether A/B are an I2C bus or free for the program (Section 9). |
| **Boot window** | The time after power-on in which a module can be stopped before its program starts. |

## 6. Safety invariants

A compliant module MUST keep these rules regardless of its program:

1. **Quiet at power-on.** All signal pins are high-impedance, with no pulls
   enabled on A or B.
2. **Boot window.** Every power-on begins with a boot window of at least
   500 ms. Programs cannot remove it.
3. **Nothing drives C/D early.** C and D are not driven until the boot window
   has ended and a program or console has claimed them.
4. **A/B are a bus.** In networked mode the module drives A/B only as an I2C
   slave, by pulling low.
5. **No pulls on A/B**, except the brief probe in Section 9.2.
6. **Contradictions stop the program.** A program whose pin declaration
   contradicts what the module observes does not run; the module reports a
   fault.
7. **Atomic writes.** A new file (program or data) replaces the old one only
   after it has been received completely and verified.
8. **Always recoverable.** A module powered up on an I2C bus can always be
   halted during its boot window, by its address (or by broadcast, if it
   implements broadcasts).

## 7. Physical and electrical

### 7.1 Pinout

| Pin | Name | Function |
|---|---|---|
| 1 | A | Global I2C SCL (module is slave) |
| 2 | B | Global I2C SDA (module is slave) |
| 3 | C | Local I/O. UART: module receives on C |
| 4 | D | Local I/O. UART: module transmits on D |
| 5 | GND | Ground |
| 6 | 3V3 | 3.3V, supplied by the carrier |

Contacts are on a 2.54 mm pitch and match one row of a 6-pin Pmod™-compatible
connector. Mechanical form factors are specified per module family.

**UART direction:** a host connects its TX to C and its RX to D.

### 7.2 Electrical rules

- Signals are 3.3V CMOS. A module MUST NOT drive pin 6.
- Maximum module supply current: **TBD**.
- A module SHOULD protect its contacts against ESD. Spring-contact modules are
  handled by people.

Carriers and hosts:

- MUST define the idle level of every C/D signal they use, so that a module
  that is absent, booting or halted leaves the carrier in a safe state.
- SHOULD use pulls of 10 kΩ or stronger on lines they receive from the module.
- MUST current-limit anything they drive onto C/D (for example with a
  series resistor of about 1 kΩ).
- Provide the pull-ups for A/B. Modules never do. Resistors of 2.2-10 kΩ
  are recommended; a carrier that has only weak internal pull-ups (tens of
  kΩ) MUST clock the bus slowly enough for its rising edges (about 10 kHz),
  and should expect noise from neighbouring lines.
- MUST NOT drive or pull up signal pins while the module is unpowered, if
  they can switch its supply.
- SHOULD keep modules away from heat. Every wear-out mechanism is slower at
  lower temperature.

## 8. Power-on sequence

1. **Power-on.** Invariant 1.
2. **Boot window**, at least 500 ms and SHOULD NOT exceed 1000 ms. The module
   answers I2C at its address and to broadcasts, listens passively on C for
   a UART console wake (if it has a UART console), and does not drive C/D.
   Any I2C transaction addressed to the module, or HOLD (if implemented),
   restarts the window.
3. **Then:**
   - console wake received: the console starts,
   - HALT received: the module waits for a master,
   - program present and its pin declaration consistent: the program runs,
   - program present but inconsistent: fault, the program does not run,
   - no program: the module idles, stays reachable over I2C, and keeps
     listening for a console wake.

On a Pmod port the supply is usually always on, so the window starts when the
module is inserted. A master that misses it can still halt a networked
module afterwards.

## 9. Networked and stand-alone

### 9.1 Rules

- The I2C slave is active during the boot window in all cases.
- A master addressing the module during the boot window makes it
  networked.
- Programs MAY read A/B as inputs in any mode, and MAY ask for the I2C slave
  to be switched off after the boot window (for example for buttons with
  pull-ups).
- A program that **drives** A/B runs only in stand-alone mode. Otherwise it
  does not start, and the module reports a fault.

### 9.2 Stand-alone probe

Performed only if the program declares that it drives A/B, after the boot
window, and only if no master addressed the module:

1. Briefly enable the internal pull-down on A, read A, disable it. Same for B.
2. If both read low, the module is stand-alone. Otherwise, including any
   ambiguous result, it is networked.

The probe is harmless on a real bus. A stand-alone carrier that wants its
module to drive A/B MUST NOT pull up both A and B.

A module that cannot afford the probe MAY rely on the first condition only
(no master has addressed it); it then cannot detect a bus whose
master is silent.

## 10. Pin declaration

Every program (or fixed-function firmware) declares how it uses each pin, in
the order A, B, C, D:

```
pins=NET,NET,OD,AIN
```

| Mode | Meaning | Pins |
|---|---|---|
| `-` | unused (high-impedance) | any |
| `IN` | digital input | any |
| `OD` | open-drain output | C, D; A, B only stand-alone |
| `PP` | push-pull output | C, D; A, B only stand-alone |
| `AIN` | analog input | C, D, if supported |
| `I2C` | I2C master (C=SCL, D=SDA) | C and D together |
| `UART` | UART to a peripheral (C=RX, D=TX) | C and D together |
| `NET` | Sechs I2C slave | A and B together (default) |

The declaration SHOULD be readable over I2C without running the program
(`pins` in INFO, Section 11.5). How a program states it is up to the
language.

A module with an ADC MAY check C/D before starting a program, without
enabling any pull. For example, a pin declared as an output that reads as a
steady mid-level voltage is driven by something else, so the program does not
run.

## 11. I2C protocol (core)

Every Sechs module implements this section.

### 11.1 Bus

- 7-bit addresses, 0x08-0x77. The firmware has a default address, suggested
  **0x0C**. The current address is stored persistently. A module never uses
  a stored address outside 0x08-0x77 (a damaged one): it uses its default.
- 100 kHz MUST be supported. Modules MAY stretch the clock, and masters
  MUST support clock stretching.
- Masters discover modules by probing each address with an address-only
  transaction, then reading the signature.

### 11.2 Register access

```
Write:  START addr+W reg data...                    STOP
Read:   START addr+W reg RESTART addr+R data...     STOP
```

The register number advances after each byte, except for **port** registers
(P), which stream bytes through one register number.

### 11.3 Broadcast

Broadcasts use the general call address and start with **0x5A**, so they are
not mistaken for the general call commands defined by the I2C specification:

```
START 0x00+W 0x5A cmd args... STOP
```

Commands use the same codes as CONTROL (Section 11.4). Broadcasts are
optional (CAPS bit 5); a module that implements them MUST accept HALT, RUN,
RESET and HOLD, and ignores any other general call. Modules that share one
address (for example a batch on a programmer) all receive the same writes
without broadcasts.

Several modules may share an address (for example after programming a batch).
Writes then reach all of them, and reads return the bitwise AND of their
replies. The OK register is designed for this: every bit is 1 for "good", so a
1 read from a shared address means "good on every module".

### 11.4 Core registers

| Reg | Name | R/W | Meaning |
|---|---|---|---|
| 0x00 | SIG0 | R | `S` (0x53) |
| 0x01 | SIG1 | R | `6` (0x36) |
| 0x02 | VER | R | spec version: major in bits 7-4, minor in bits 3-0 |
| 0x03 | CAPS | R | bit 0 file commands on the consoles, 1 UART console, 2 I2C console, 3 writable label, 4 identify, 5 broadcasts, 6 HOLD, 7 programming mode |
| 0x04 | STATUS | R | bit 0 boot window, 1 halted, 2 running, 3 console active, 4 networked, 5 fault, 6 degraded |
| 0x05 | OK | R | bit 0 alive, 1 no fault, 2 not degraded, 3 not halted, 4 last console command succeeded |
| 0x06 | FAULT | R | 0 none, 1 program drives A/B on a bus, 2 C/D contradict declaration, 3 program error, 4 storage check failed |
| 0x07 | CONTROL | W | 0x01 HALT, 0x02 RUN, 0x03 RESET; optional: 0x04 HOLD (CAPS bit 6), 0x05 IDENTIFY (CAPS bit 4), 0x06 PROGRAM (CAPS bit 7) |
| 0x08 | ADDR | W | two bytes, written to this one register: new address, then its bitwise complement |
| 0x09 | INFO | R P | identity text (Section 11.5) |

- **ADDR:** the new address takes effect after the STOP and is stored
  persistently, only if the complement matches.
- **IDENTIFY:** if CAPS bit 4 is set, the module makes itself visible for a
  few seconds (for example by blinking an LED), so a person can find it.
- **PROGRAM:** if CAPS bit 7 is set, the module restarts in its programming
  mode, in which its firmware can be written (for example through SWIO).
  What that mode is depends on the module, and its documentation MUST say:
  which pins it uses, how long it lasts, and how it ends. A module whose
  programming pin is pin A (shared with the Sechs bus) answers no I2C while
  in programming mode; a master MUST NOT use the bus during programming.
  The module MUST return to normal operation by itself if nothing programs
  it.
- **Degraded:** the module still works but has detected and corrected a fault
  in its own storage, or otherwise expects to fail. It should be replaced.

Registers 0x0A-0x17 and 0x1B-0x7F are reserved and read as 0. Registers
0x80-0xFF belong to the running program (Section 13).

**Note for implementers:** many I2C peripherals load the next byte to send
before the master has acknowledged the previous one. A read of a port
register such as CDATA must not lose that byte when the master ends the
read: the reference implementation removes bytes from the output buffer
only when the transfer ends, minus any byte that was loaded but not sent.
A byte loaded after the buffer ran empty (a filler) was never taken from
it, and must not be counted back.

### 11.5 INFO

Reading INFO from the start (any transaction that writes register 0x09 resets
it) returns ASCII text: lines of `key=value`, each ending in LF, terminated by
a zero byte.

| Key | Required | Example |
|---|---|---|
| `fw` | yes | `Machdyne BASIC 1.0` |
| `mod` | yes | `Zwolf LS10A` |
| `pins` | no | `NET,NET,OD,AIN` |
| `lang` | with files profile | `basic` |
| `prog` | no | `BOOT.BAS` (the file run at boot) |
| `label` | no | `PANEL-3-TIMER` |
| `iface` | no | `tidegrow.timer/1` |
| `uid` | no | unique ID in hex |

- `lang` names the program format the module accepts, so a programmer knows
  what to send: source text for a module that interprets it, or a compiled
  format for a module that does not.
- `label` is a name set by a person (Section 12.3).
- `iface` names the protocol a program offers in the program registers, so
  that a master can drive any module offering it without knowing the
  program.

A fixed-function firmware can return a constant INFO string.

## 12. Optional profiles

### 12.1 Files through the consoles (CAPS bit 0)

Files are handled by commands typed at a console, so the UART console and
the I2C console give the same two paths to the same files, with no
separate file protocol:

| Command | Meaning |
|---|---|
| `SAVE name` | save the program in memory |
| `LOAD name` | load a program |
| `TYPE name` | print a file (programs and data, such as logs) |
| `DIR` | list the files |
| `DEL name` | delete a file |

- File names are 8.3, ASCII, case-insensitive (for example `BOOT.BAS`,
  `LOG.DAT`).
- `BOOT.BAS` is the program run at boot.
- A module MUST be able to print every file in the form it was stored
  (`TYPE`), so that programs and data can be recovered before a module is
  replaced.
- Saving replaces a file atomically (invariant 7).
- OK bit 4 says whether the last command succeeded. After typing the same
  lines into several modules that share one address (for example a batch
  on a programmer), one read of OK tells the master whether the command
  succeeded on every module. A master writing to modules that share an
  address cannot read CIN for each of them, so it paces its writes instead.

### 12.2 Consoles (CAPS bits 1 and 2)

A console is an interactive session with the firmware.

**UART console on C/D:**

- Wake: a correctly framed CR (0x0D) or LF (0x0A) on C, during the boot
  window or while idle with no program. Other bytes do not wake it.
- **Rate:** a module MUST use either 9600 or 115200 baud, 8N1, and its
  documentation MUST say which. A host MUST support both.
- **Answer:** a module MUST answer a wake with some output (for example a
  banner or prompt) within 100 ms. A host that does not know the module's
  rate sends the wake at one rate and, if nothing is received, at the
  other.
- Before waking, the module only listens on C. After waking, D is the
  transmit line.
- Nothing but the terminal may be connected to C/D while the UART console is
  in use. It is a bench console, not for use in carriers.
- Running a program that uses C/D from the UART console takes the console's
  pins: the console stops until the pins are free again. The module SHOULD
  say so before it happens.

**I2C console on A/B:**

| Reg | Name | R/W | Meaning |
|---|---|---|---|
| 0x18 | CIN | R | bytes the module can accept |
| 0x19 | COUT | R | bytes waiting to be read |
| 0x1A | CDATA | R/W P | write: input, read: output |

- A master reads at most COUT bytes from CDATA at a time.
- When its output buffer is full, a module waits for the master to
  read. If nothing is read for about half a second, it stops sending
  output to the I2C console until the master writes CDATA again, so a
  master that goes away never stops a program.
- A module may take a moment to start answering (a LOAD, a program that
  waits); a master treats the console as finished only after it has
  been quiet for a while (`sechsctl`: 300 ms).

### 12.3 Writable label (CAPS bit 3)

| Reg | Name | R/W | Meaning |
|---|---|---|---|
| 0x1C | LABEL | W P | new label, up to 32 bytes, stored persistently at STOP |

## 13. Program registers

Registers 0x80-0xFF are for the running program to exchange data with
masters. Their meaning is defined by the program and named by `iface`.
Firmware SHOULD give programs access to at least some of them; Machdyne
BASIC provides 0x80-0x8F as `REG 0` to `REG 15`. They are not cleared when
a program starts, so a master can set them before `RUN`.

## 14. Longevity

Sechs modules are designed to keep their programs for a very long time, even
when the module itself does not last that long.

- **Programs and data outlive modules.** `TYPE` (Section 12.1)
  lets a programmer copy programs and logs off a module before replacing
  it. Masters SHOULD keep copies of the programs of the modules they
  manage.
- **Report before failing.** A module that can check its own storage SHOULD
  do so and set the degraded flag when it finds and corrects errors.
- **Renewal.** If the firmware's own storage wears out, it can be renewed by
  reinstalling the firmware (outside the scope of this spec) after reading
  the files off and before loading them back.

## 15. Example: Tidegrow Grow

| Pin | Name | Function |
|---|---|---|
| 1 | A | I2C SCL, chained to other panels |
| 2 | B | I2C SDA, chained to other panels |
| 3 | C | EN: floating = light on, low = off |
| 4 | D | TEMP: board temperature, 0-1.1V |
| 5 | GND | |
| 6 | 3V3 | |

The light is on while the module is absent, booting or halted. The module
never drives D. C never carries a framed CR, so the UART console cannot wake
by accident. A timer program declares `pins=NET,NET,OD,AIN`.

To check: the strength of the EN pull-up (Section 7.2), and the module's
temperature on the light's aluminium board.

## 16. Open issues

- Maximum supply current.
- Broadcasting console input to modules with different addresses (today:
  give them one shared address while programming them).
- Mechanical drawings for six-pin-only modules.
- Final register numbers and command codes.
- Automatic resolution of duplicate addresses.
