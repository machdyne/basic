#!/bin/bash
#
# hwtest.sh: test a Sechs module running Machdyne BASIC (LS10) through
# sechsctl, for example with the Werkzeug bridge:
#
#   tools/sechs/hwtest.sh -d /dev/ttyACM0 [ADDR]
#   tools/sechs/hwtest.sh -b 1 [ADDR]               (a Linux I2C bus)
#   tools/sechs/hwtest.sh -d /dev/ttyACM0 --uart    (also the UART console;
#                                                    reset the bridge after)
#
# ADDR defaults to 0x0c. The module's storage must be formatted
# (FORMAT YES on its console); the test does not format it. An existing
# BOOT.BAS is kept and restored. Set SECHSCTL to the sechsctl to use.

SECHSCTL=${SECHSCTL:-$(dirname "$0")/../../sechsctl}
T=()
A=0x0c
UART=0
while [ $# -gt 0 ]; do
    case "$1" in
        -d|-b) T=("$1" "$2"); shift 2 ;;
        --uart) UART=1; shift ;;
        *) A=$1; shift ;;
    esac
done
[ ${#T[@]} -eq 0 ] && { echo "usage: $0 -d DEV | -b BUS [ADDR] [--uart]"; exit 2; }

PASS=0
FAIL=0
ctl() { "$SECHSCTL" "${T[@]}" "$@"; }
# type lines into the module's console and print what it answers
con() { printf '%s\n' "$@" | ctl send "$A" 2>/dev/null | tr -d '\r'; }
ok() { echo "PASS  $1"; PASS=$((PASS + 1)); }
bad() { echo "FAIL  $1${2:+: $2}"; FAIL=$((FAIL + 1)); }
check() {   # name, then a command that must succeed
    local name=$1
    shift
    if "$@"; then ok "$name"; else bad "$name"; fi
}
# wait up to 2 s for a register to have a value
reg_is() {
    for i in $(seq 1 20); do
        [ "$(ctl reg "$A" "$1" 2>/dev/null)" = "$2" ] && return 0
        sleep 0.1
    done
    return 1
}
fault_is() { ctl info "$A" 2>/dev/null | grep -q "^fault   $1\$"; }
status_has() { ctl info "$A" 2>/dev/null | grep -q "^status .*$1"; }
status_lacks() {
    for i in $(seq 1 20); do
        status_has "$1" || return 0
        sleep 0.1
    done
    return 1
}

echo "Sechs module test: ${T[*]}, address $A"

# ---- identity ----
info=$(ctl info "$A" 2>&1)
if echo "$info" | grep -q "^version 0.5" && echo "$info" | grep -q "^fw=Machdyne BASIC"; then
    ok "identity (S6, version 0.5, INFO)"
else
    bad "identity" "$(echo "$info" | head -1)"
    echo "no module to test"
    exit 1
fi

# ---- console ----
ctl halt "$A"
out=$(con "NEW" "10 PRINT 6 * 7" "RUN")
check "I2C console runs a program" grep -q "^42$" <<< "$out"

# ---- files ----
out=$(con "DIR")
if grep -q "NOT FORMATTED" <<< "$out"; then
    bad "storage" "not formatted (type FORMAT YES on the console)"
else
    con "SAVE HWTEST" > /dev/null
    out=$(con "NEW" "LOAD HWTEST" "LIST")
    check "SAVE and LOAD" grep -q "^10 PRINT 6 \* 7$" <<< "$out"
    out=$(con "TYPE HWTEST")
    check "TYPE" grep -q "^10 PRINT 6 \* 7$" <<< "$out"
    out=$(con "DEL HWTEST" "DIR")
    check "DEL" bash -c "! grep -q HWTEST.BAS <<< \"\$1\"" _ "$out"
    out=$(con "NEW" \
        '10 OPEN "HWTEST.DAT" FOR OUTPUT AS 1: PRINT #1, 5: CLOSE 1' \
        '20 OPEN "HWTEST.DAT" FOR APPEND AS 1: PRINT #1, 7: CLOSE 1' \
        '30 OPEN "HWTEST.DAT" FOR INPUT AS 1: INPUT #1, A: INPUT #1, B: CLOSE 1' \
        '40 PRINT "SUM"; A + B' "RUN" "DEL HWTEST.DAT")
    check "data files (output, append, input)" grep -q "^SUM12$" <<< "$out"
fi

# ---- a running program and the master ----
con "NEW" "10 REG 1, REG(0) + 1" "20 GOTO 10" "RUN" > /dev/null
check "program running (STATUS)" status_has running
ctl reg "$A" 0 5
check "registers: master and program exchange values" reg_is 1 6
ctl reg "$A" 0 41
check "registers: a second value" reg_is 1 42
ctl halt "$A"
check "HALT stops the program" status_lacks running
out=$(con "")
check "the console reports BREAK" grep -q "^BREAK IN" <<< "$out"

# ---- faults ----
con "NEW" "10 PRINT 1 / 0" "RUN" > /dev/null
check "a program error sets FAULT (3: program error)" fault_is 3
out=$(con "NEW" "10 PINS PP,-,-,-" "RUN")
check "pins 1 and 2 refused on a bus (ON A BUS)" grep -q "ON A BUS" <<< "$out"
con "NEW" "10 PRINT 1" "RUN" > /dev/null
check "a good run clears FAULT" fault_is 0

# ---- address ----
if ctl addr "$A" 0x21 && ctl info 0x21 > /dev/null 2>&1 && \
   ! ctl info "$A" > /dev/null 2>&1; then
    ok "address change"
else
    bad "address change"
fi
if ctl addr 0x21 "$A" 2>/dev/null; then
    ok "address change back"
else
    bad "address change back" "the module may now be at 0x21"
fi

# ---- reset, BOOT.BAS and storage across a reset ----
if ! grep -q "NOT FORMATTED" <<< "$(con DIR)"; then
    had_boot=0
    grep -q "^BOOT.BAS$" <<< "$(con DIR)" && had_boot=1
    [ $had_boot = 1 ] && con "LOAD BOOT" "SAVE HWBOOT" > /dev/null
    con "NEW" "10 REG 2, 77" "SAVE BOOT" > /dev/null
    ctl reg "$A" 3 9
    ctl reset "$A"
    sleep 1.5       # the boot window, then BOOT.BAS
    check "RESET runs BOOT.BAS from storage" reg_is 2 77
    check "RESET clears the registers" reg_is 3 0
    if [ $had_boot = 1 ]; then
        con "LOAD HWBOOT" "SAVE BOOT" "DEL HWBOOT" > /dev/null
        echo "      (BOOT.BAS restored)"
    else
        con "DEL BOOT" > /dev/null
    fi
fi
con "NEW" > /dev/null

# ---- UART console (bridge only, last: the bridge stays in UART mode) ----
if [ $UART = 1 ]; then
    out=$( (printf '\r'; sleep 1; printf 'PRINT\r'; sleep 0.5) | ctl uart 115200 2>/dev/null | tr -d '\r')
    check "UART console wakes and answers (///)" grep -q "///" <<< "$out"
    echo "      reset the bridge (unplug it or press RESET) before using it again"
fi

echo "$PASS passed, $FAIL failed"
[ $FAIL = 0 ]
