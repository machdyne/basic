#!/bin/bash

# BASIC Interpreter Test Suite
# Tests all language features to ensure correctness

# Every test runs; the summary sets the exit status.

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0
TOTAL=0

# Compile the interpreter
compile_basic() {
    echo "Compiling BASIC interpreter..."
    gcc -std=gnu99 -Wall -DTARGET_LINUX -o basic basic.c 2>&1
    if [ $? -ne 0 ]; then
        echo -e "${RED}FATAL: Failed to compile basic.c${NC}"
        exit 1
    fi
    echo -e "${GREEN}Compilation successful${NC}"
    echo ""
}

# Test runner function
run_test() {
    local test_name="$1"
    local program="$2"
    local expected="$3"
    
    TOTAL=$((TOTAL + 1))
    
    # Run the program and capture output
    # Remove prompts (>), banner (///), carriage returns, and empty lines
    actual=$(printf "%s\n" "$program" | ./basic 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | tr -d '\r' | grep -v '^$')
    
    # Compare output
    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# Print section header
section() {
    echo ""
    echo -e "${YELLOW}=== $1 ===${NC}"
}

# Compile first
compile_basic

# ============================================================
section "Basic PRINT Tests"
# ============================================================

run_test "PRINT string" \
"10 PRINT \"Hello\"
RUN" \
"Hello"

run_test "PRINT number" \
"10 PRINT 42
RUN" \
"42"

run_test "PRINT multiple lines" \
"10 PRINT \"Line 1\"
20 PRINT \"Line 2\"
RUN" \
"Line 1
Line 2"

# ============================================================
section "Variable Assignment (LET)"
# ============================================================

run_test "LET and PRINT variable" \
"10 LET A = 5
20 PRINT A
RUN" \
"5"

run_test "LET multiple variables" \
"10 LET A = 10
20 LET B = 20
30 PRINT A
40 PRINT B
RUN" \
"10
20"

run_test "LET variable overwrite" \
"10 LET A = 5
20 LET A = 10
30 PRINT A
RUN" \
"10"

# ============================================================
section "Arithmetic Operations"
# ============================================================

run_test "Addition" \
"10 LET A = 5 + 3
20 PRINT A
RUN" \
"8"

run_test "Subtraction" \
"10 LET A = 10 - 3
20 PRINT A
RUN" \
"7"

run_test "Multiplication" \
"10 LET A = 6 * 7
20 PRINT A
RUN" \
"42"

run_test "Division" \
"10 LET A = 20 / 4
20 PRINT A
RUN" \
"5"

run_test "Complex expression" \
"10 LET A = 10 + 5 * 2
20 PRINT A
RUN" \
"20"

run_test "Parentheses" \
"10 LET A = (10 + 5) * 2
20 PRINT A
RUN" \
"30"

run_test "Negative numbers" \
"10 LET A = 5 - 10
20 PRINT A
RUN" \
"-5"

run_test "Variable arithmetic" \
"10 LET A = 5
20 LET B = 3
30 LET C = A + B
40 PRINT C
RUN" \
"8"

# ============================================================
section "GOTO Statement"
# ============================================================

run_test "GOTO forward" \
"10 GOTO 30
20 PRINT \"Skip\"
30 PRINT \"Jump\"
RUN" \
"Jump"

run_test "GOTO backward (loop)" \
"10 LET A = 0
20 LET A = A + 1
30 PRINT A
40 IF A < 3 THEN GOTO 20
RUN" \
"1
2
3"

# ============================================================
section "IF/THEN Statement"
# ============================================================

run_test "IF THEN true condition" \
"10 LET A = 5
20 IF A == 5 THEN PRINT \"Yes\"
30 PRINT \"Done\"
RUN" \
"Yes
Done"

run_test "IF THEN false condition" \
"10 LET A = 3
20 IF A == 5 THEN PRINT \"No\"
30 PRINT \"Done\"
RUN" \
"Done"

run_test "IF THEN with GOTO" \
"10 LET A = 1
20 IF A == 1 THEN GOTO 40
30 PRINT \"Skip\"
40 PRINT \"Jump\"
RUN" \
"Jump"

# ============================================================
section "IF/THEN/ELSE Statement"
# ============================================================

run_test "IF THEN ELSE - true branch" \
"10 LET A = 5
20 IF A == 5 THEN PRINT \"True\" ELSE PRINT \"False\"
RUN" \
"True"

run_test "IF THEN ELSE - false branch" \
"10 LET A = 3
20 IF A == 5 THEN PRINT \"True\" ELSE PRINT \"False\"
RUN" \
"False"

run_test "IF THEN ELSE with multiple statements" \
"10 LET A = 5
20 IF A == 5 THEN LET B = 1 ELSE LET B = 2
30 PRINT B
RUN" \
"1"

# ============================================================
section "Comparison Operators"
# ============================================================

run_test "Equality (==)" \
"10 IF 5 == 5 THEN PRINT \"Equal\"
RUN" \
"Equal"

run_test "Less than (<)" \
"10 IF 3 < 5 THEN PRINT \"Less\"
RUN" \
"Less"

run_test "Greater than (>)" \
"10 IF 7 > 5 THEN PRINT \"Greater\"
RUN" \
"Greater"

run_test "Less than or equal (<=)" \
"10 IF 5 <= 5 THEN PRINT \"LessOrEqual\"
RUN" \
"LessOrEqual"

run_test "Greater than or equal (>=)" \
"10 IF 5 >= 5 THEN PRINT \"GreaterOrEqual\"
RUN" \
"GreaterOrEqual"

run_test "Not equal (<>)" \
"10 IF 3 <> 5 THEN PRINT \"NotEqual\"
RUN" \
"NotEqual"

run_test "Comparison with variables" \
"10 LET A = 10
20 LET B = 5
30 IF A > B THEN PRINT \"A bigger\"
RUN" \
"A bigger"

# ============================================================
section "END Statement"
# ============================================================

run_test "END stops execution" \
"10 PRINT \"Before\"
20 END
30 PRINT \"After\"
RUN" \
"Before"

# ============================================================
section "Complex Programs"
# ============================================================

run_test "Fibonacci sequence" \
"10 LET A = 0
20 LET B = 1
30 PRINT A
40 PRINT B
50 LET C = A + B
60 PRINT C
70 LET A = B
80 LET B = C
90 IF C < 20 THEN GOTO 50
RUN" \
"0
1
1
2
3
5
8
13
21"

run_test "Countdown" \
"10 LET A = 5
20 PRINT A
30 LET A = A - 1
40 IF A > 0 THEN GOTO 20
50 PRINT \"Done\"
RUN" \
"5
4
3
2
1
Done"

run_test "Multiple conditions" \
"10 LET A = 5
20 IF A > 3 THEN PRINT \"Greater than 3\"
30 IF A < 10 THEN PRINT \"Less than 10\"
40 IF A == 5 THEN PRINT \"Equal to 5\"
RUN" \
"Greater than 3
Less than 10
Equal to 5"

run_test "Variable reuse" \
"10 LET A = 1
20 PRINT A
30 LET A = 2
40 PRINT A
50 LET A = 3
60 PRINT A
RUN" \
"1
2
3"

# ============================================================

# ============================================================
section "String Handling"
# ============================================================

run_test "String with spaces" \
"10 PRINT \"Hello World\"
RUN" \
"Hello World"

run_test "String with numbers" \
"10 PRINT \"Test 123\"
RUN" \
"Test 123"

# ============================================================
section "Edge Cases"
# ============================================================

run_test "Zero value" \
"10 LET A = 0
20 PRINT A
RUN" \
"0"

run_test "Division by non-zero" \
"10 LET A = 100 / 10
20 PRINT A
RUN" \
"10"

run_test "Large numbers" \
"10 LET A = 1000 + 2000
20 PRINT A
RUN" \
"3000"

run_test "All 26 variables" \
"10 LET A = 1
20 LET Z = 26
30 PRINT A
40 PRINT Z
RUN" \
"1
26"

# ============================================================
section "LIST Command"
# ============================================================

# Test LIST output
TOTAL=$((TOTAL + 1))
list_output=$(printf "10 PRINT \"Test\"\nLIST\n" | ./basic 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | tr -d '\r' | grep -v '^$')
if echo "$list_output" | grep -q "10 PRINT \"Test\""; then
    echo -e "${GREEN}✓${NC} LIST command"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} LIST command"
    echo "  Output: $list_output"
    FAILED=$((FAILED + 1))
fi

# ============================================================
section "Line Deletion"
# ============================================================

TOTAL=$((TOTAL + 1))
delete_output=$(printf "10 PRINT \"Keep\"\n20 PRINT \"Delete\"\n20\nLIST\n" | ./basic 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | tr -d '\r' | grep -v '^$')
if echo "$delete_output" | grep -q "10 PRINT \"Keep\"" && ! echo "$delete_output" | grep -q "20"; then
    echo -e "${GREEN}✓${NC} Line deletion"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Line deletion"
    echo "  Output: $delete_output"
    FAILED=$((FAILED + 1))
fi

# ============================================================
section "Line Replacement"
# ============================================================

TOTAL=$((TOTAL + 1))
replace_output=$(printf "10 PRINT \"Old\"\n10 PRINT \"New\"\nLIST\n" | ./basic 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | tr -d '\r' | grep -v '^$')
if echo "$replace_output" | grep -q "10 PRINT \"New\"" && ! echo "$replace_output" | grep -q "Old"; then
    echo -e "${GREEN}✓${NC} Line replacement"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Line replacement"
    echo "  Output: $replace_output"
    FAILED=$((FAILED + 1))
fi

# ============================================================
section "SAVE and LOAD"
# ============================================================

# Clean up any existing test file
rm -f ZZTEST.BAS

TOTAL=$((TOTAL + 1))
save_load_output=$(printf "10 PRINT \"Test\"\n20 LET A = 42\nSAVE test_suite_temp.bas\nLOAD test_suite_temp.bas\nLIST\n" | ./basic 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | grep -v "Saved" | grep -v "Loaded" | tr -d '\r' | grep -v '^$')

if echo "$save_load_output" | grep -q "10 PRINT \"Test\"" && echo "$save_load_output" | grep -q "20 LET A = 42"; then
    echo -e "${GREEN}✓${NC} SAVE and LOAD"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} SAVE and LOAD"
    echo "  Output: $save_load_output"
    FAILED=$((FAILED + 1))
fi

# Clean up
rm -f ZZTEST.BAS

# ============================================================
section "Program Ordering"
# ============================================================

run_test "Lines execute in order" \
"30 PRINT \"Third\"
10 PRINT \"First\"
20 PRINT \"Second\"
RUN" \
"First
Second
Third"

run_test "Line insertion maintains order" \
"10 PRINT \"First\"
30 PRINT \"Third\"
20 PRINT \"Second\"
RUN" \
"First
Second
Third"

# ============================================================
# Final Summary
# ============================================================
section "Keywords and Syntax"
# ============================================================

run_test "Lower-case keywords" \
"10 print 42
20 let a = 5
30 if a == 5 then print a
run" \
"42
5"

run_test "Keywords without spaces" \
"10 LETA=3
20 IFA>2THENPRINTA
RUN" \
"3"

run_test "Unary minus" \
"10 LET A = -5 * 2
20 PRINT A
30 PRINT -(A - 1)
RUN" \
"-10
11"

run_test "Two-letter variable is a syntax error" \
"10 PRINT AB
LIST" \
"SYNTAX ERROR"

run_test "Unknown command" \
"HELLO" \
"SYNTAX ERROR"

run_test "Unterminated string" \
"10 PRINT \"ABC" \
"SYNTAX ERROR"

run_test "Number too large" \
"10 PRINT 32768" \
"TOO BIG"

run_test "Largest number" \
"10 PRINT 32767
RUN" \
"32767"

run_test "Line too long" \
"10 PRINT 1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1
LIST" \
"TOO LONG"

run_test "Line number out of range" \
"40000 PRINT 1" \
"TOO BIG"

# ============================================================
section "Runtime Errors"
# ============================================================

run_test "Division by zero" \
"10 PRINT 1 / 0
RUN" \
"DIV BY 0 IN 10"

run_test "Undefined line" \
"10 GOTO 99
RUN" \
"NO LINE IN 10"

run_test "Error stops the program" \
"10 PRINT 1
20 LET A = 5 / 0
30 PRINT 2
RUN" \
"1
DIV BY 0 IN 20"

# ============================================================
section "Commands"
# ============================================================

run_test "NEW clears the program" \
"10 PRINT 1
NEW
LIST
RUN" \
""

run_test "RUN clears variables" \
"10 PRINT A
20 LET A = 7
RUN
RUN" \
"0
0"

run_test "HELP lists commands and keywords" \
"HELP
help" \
"RUN LIST NEW SAVE LOAD DIR DEL TYPE FORMAT HELP 
ADC AND APPEND AS CLOSE ELSE END EOF FOR GOSUB GOTO I2CR I2C IF INPUT IN LED LET MOD NEXT NOT OPEN OR OUTPUT OUT PINS PRINT REG REM RETURN SLEEP STEP THEN TO WAIT 
RUN LIST NEW SAVE LOAD DIR DEL TYPE FORMAT HELP 
ADC AND APPEND AS CLOSE ELSE END EOF FOR GOSUB GOTO I2CR I2C IF INPUT IN LED LET MOD NEXT NOT OPEN OR OUTPUT OUT PINS PRINT REG REM RETURN SLEEP STEP THEN TO WAIT "

run_test "Commands need a whole word" \
"LISTX
RUNNING" \
"SYNTAX ERROR
SYNTAX ERROR"

run_test "FORMAT needs YES" \
"FORMAT" \
"SYNTAX ERROR"

run_test "FORMAT is not supported on Linux" \
"FORMAT YES" \
"NOT SUPPORTED"

# ============================================================
section "Time"
# ============================================================

TOTAL=$((TOTAL + 1))
start=$(date +%s%N)
out=$(printf "10 WAIT 300\n20 PRINT 1\nRUN\n" | ./basic 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -x "1")
ms=$(( ($(date +%s%N) - start) / 1000000 ))
if [ "$out" = "1" ] && [ $ms -ge 300 ] && [ $ms -lt 1000 ]; then
    echo -e "${GREEN}✓${NC} WAIT 300 (${ms} ms)"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} WAIT 300 (${ms} ms, output '$out')"
    FAILED=$((FAILED + 1))
fi

TOTAL=$((TOTAL + 1))
start=$(date +%s%N)
out=$(printf "10 SLEEP 1\n20 PRINT 1\nRUN\n" | ./basic 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -x "1")
ms=$(( ($(date +%s%N) - start) / 1000000 ))
if [ "$out" = "1" ] && [ $ms -ge 1000 ] && [ $ms -lt 2000 ]; then
    echo -e "${GREEN}✓${NC} SLEEP 1 (${ms} ms)"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} SLEEP 1 (${ms} ms, output '$out')"
    FAILED=$((FAILED + 1))
fi

# Ctrl-C stops a running program
TOTAL=$((TOTAL + 1))
(printf "10 GOTO 10\nRUN\n"; sleep 1) | ./basic > /tmp/basic_break.out 2>&1 &
pid=$!
sleep 0.3
kill -INT $(pgrep -n -x basic) 2>/dev/null
wait $pid
if tr -d '\r' < /tmp/basic_break.out | grep -q "BREAK IN 10"; then
    echo -e "${GREEN}✓${NC} Ctrl-C stops the program"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Ctrl-C stops the program"
    FAILED=$((FAILED + 1))
fi

# ============================================================
section "Files"
# ============================================================

rm -f ZZ*

run_test "LOAD a missing file" \
"LOAD zznone" \
"NOT FOUND"

run_test "Bad file names" \
"SAVE zzlongname
SAVE zz.basic
SAVE zz.a.b
SAVE" \
"BAD NAME
BAD NAME
BAD NAME
BAD NAME"

run_test "SAVE writes text, LOAD reads it" \
"10 PRINT \"HI\"
20 GOTO 10
SAVE zzprog
NEW
LOAD ZZPROG.BAS
LIST" \
"10 PRINT \"HI\"
20 GOTO 10"

TOTAL=$((TOTAL + 1))
if [ "$(cat ZZPROG.BAS)" = "$(printf '10 PRINT "HI"\n20 GOTO 10')" ]; then
    echo -e "${GREEN}✓${NC} Saved file is plain text with LF line endings"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Saved file is plain text with LF line endings"
    od -c ZZPROG.BAS | head -5
    FAILED=$((FAILED + 1))
fi

run_test "DIR lists files" \
"DIR" \
"ZZPROG.BAS"

run_test "TYPE prints a file" \
"TYPE zzprog
TYPE zznone" \
"10 PRINT \"HI\"
20 GOTO 10
NOT FOUND"

run_test "DEL removes a file" \
"DEL zzprog
LOAD zzprog" \
"NOT FOUND"

# LIST -> SAVE -> NEW -> LOAD -> LIST must give the same listing
TOTAL=$((TOTAL + 1))
prog='10 LET A = (10 + 5) * -2
20 IF A < 0 THEN PRINT "NEG" ELSE PRINT "POS"
30 IF A <= -30 THEN IF A >= -30 THEN PRINT A
40 OUT 3, IN(4) - 1
50 INPUT "VALUE", B
60 IF B <> 0 THEN GOTO 80
70 PRINT B / 2
80 SLEEP 0
90 WAIT 0
100 PRINT "A, B = (C)"
110 IF A == 1 THEN END
120 END'
first=$(printf "%s\nLIST\n" "$prog" | ./basic 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///' | grep -v '^$')
second=$(printf "%s\nSAVE zzround\nNEW\nLOAD zzround\nLIST\n" "$prog" | ./basic 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///' | grep -v '^$')
if [ "$first" = "$prog" ] && [ "$second" = "$prog" ]; then
    echo -e "${GREEN}✓${NC} LIST, SAVE and LOAD round trip"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} LIST, SAVE and LOAD round trip"
    diff <(echo "$prog") <(echo "$second") | head -10
    FAILED=$((FAILED + 1))
fi

# A program with an error in it loads up to the error
printf '10 PRINT 1\n20 PRINT AB\n30 PRINT 3\n' > ZZBAD.BAS
run_test "LOAD stops at a bad line" \
"LOAD zzbad
LIST" \
"SYNTAX ERROR
10 PRINT 1"

# Appending (as on modules) is tested with data files in Phase 2.

rm -f ZZ*.BAS ZZ*.DAT ZZ*.BAS~

# ============================================================
section "Phase 2: Loops and Subroutines"
# ============================================================

run_test "FOR and NEXT" \
"10 FOR I = 1 TO 3: PRINT I;: NEXT I: PRINT
RUN" \
"123"

run_test "FOR with negative STEP" \
"10 FOR J = 10 TO 0 STEP -5: PRINT J;\" \";: NEXT: PRINT
RUN" \
"10 5 0 "

run_test "Nested FOR" \
"10 FOR I = 1 TO 2: FOR J = 1 TO 2: PRINT I * 10 + J;\" \";: NEXT J: NEXT I
20 PRINT
RUN" \
"11 12 21 22 "

run_test "FOR runs at least once" \
"10 FOR I = 5 TO 1: PRINT I: NEXT
RUN" \
"5"

run_test "NEXT without FOR" \
"10 NEXT
RUN" \
"NO FOR IN 10"

run_test "GOSUB and RETURN" \
"10 GOSUB 100: PRINT \"BACK\": END
100 PRINT \"SUB\": RETURN
RUN" \
"SUB
BACK"

run_test "RETURN without GOSUB" \
"10 RETURN
RUN" \
"NO GOSUB IN 10"

run_test "GOTO needs a line number" \
"10 GOTO A" \
"SYNTAX ERROR"

# ============================================================
section "Phase 2: Statements and Expressions"
# ============================================================

run_test "REM keeps its text" \
"10 REM   a \"quoted\" comment
LIST" \
"10 REM a \"quoted\" comment"

run_test "Statements separated by colons" \
"10 A = 1: B = 2: PRINT A + B
RUN" \
"3"

run_test "IF THEN part with colons, ELSE" \
"10 IF 1 THEN PRINT \"A\": PRINT \"B\" ELSE PRINT \"C\"
20 IF 0 THEN PRINT \"D\" ELSE PRINT \"E\": PRINT \"F\"
RUN" \
"A
B
E
F"

run_test "THEN and ELSE with line numbers" \
"10 IF 0 THEN 30 ELSE 40
30 PRINT \"WRONG\": END
40 PRINT \"RIGHT\"
RUN" \
"RIGHT"

run_test "PRINT with ; and ," \
"10 PRINT 1; 2;
20 PRINT 3, 4
RUN" \
"123           4"

run_test "MOD, comparisons, AND, OR, NOT" \
"10 PRINT 7 MOD 3; \" \"; 1 = 1; \" \"; 1 > 2; \" \";
15 PRINT 5 AND 3; \" \"; 5 OR 2; \" \"; NOT 0
20 IF 2 > 1 AND NOT 1 = 2 THEN PRINT \"OK\"
RUN" \
"1 -1 0 1 7 -1
OK"

run_test "MOD by zero" \
"10 PRINT 5 MOD 0
RUN" \
"DIV BY 0 IN 10"

# ============================================================
section "Phase 2: Pins, I2C, Registers"
# ============================================================

run_test "PINS, OUT and IN" \
"10 PINS NET,NET,PP,OD
20 OUT 3, 0: OUT 4, 1: PRINT IN(3); IN(4)
RUN" \
"01"

run_test "ADC (simulated: pin 3 reads 300)" \
"10 PINS NET,NET,AIN,AIN: PRINT ADC(3); \" \"; ADC(4)
RUN" \
"300 400"

run_test "Undeclared pin" \
"10 OUT 3, 1
RUN" \
"PIN NOT DECLARED IN 10"

run_test "Bad pin declarations" \
"10 PINS NET,IN,-,-
10 PINS -,-,I2C,-
10 PINS AIN,-,-,-
10 PINS NET,NET,-" \
"BAD PINS
BAD PINS
BAD PINS
BAD PINS"

run_test "I2C write and read (simulated device at 80)" \
"10 PINS NET,NET,I2C,I2C
20 I2C 80, 16, 42: PRINT I2CR(80, 16); \" \"; I2CR(81)
RUN" \
"42 -1"

run_test "I2C without a device" \
"10 PINS NET,NET,I2C,I2C: I2C 81, 1
RUN" \
"I2C ERROR IN 10"

run_test "I2C needs PINS" \
"10 PRINT I2CR(80)
RUN" \
"PIN NOT DECLARED IN 10"

run_test "REG" \
"10 REG 15, 200: PRINT REG(15)
20 REG 16, 1
RUN" \
"200
OUT OF RANGE IN 20"

# ============================================================
section "Phase 2: Data Files"
# ============================================================

rm -f ZZ*.DAT
run_test "OPEN, PRINT #, INPUT #, EOF" \
"10 OPEN \"zzlog.dat\" FOR OUTPUT AS #1
20 FOR I = 1 TO 3: PRINT #1, I; \",\"; I * I: NEXT
30 CLOSE #1
40 OPEN \"ZZLOG.DAT\" FOR APPEND AS 1: PRINT #1, -7: CLOSE 1
50 OPEN \"ZZLOG.DAT\" FOR INPUT AS #1
60 IF EOF(1) THEN 90
70 INPUT #1, A: PRINT A; \" \";
80 GOTO 60
90 CLOSE #1: PRINT
RUN" \
"1 1 2 4 3 9 -7 "

TOTAL=$((TOTAL + 1))
if [ "$(cat ZZLOG.DAT)" = "$(printf '1,1\n2,4\n3,9\n-7')" ]; then
    echo -e "${GREEN}✓${NC} Data file is plain text"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Data file is plain text"
    FAILED=$((FAILED + 1))
fi

run_test "Appending continues the last line" \
"10 OPEN \"ZZLOG.DAT\" FOR OUTPUT AS 1: PRINT #1, 5;: CLOSE 1
20 OPEN \"ZZLOG.DAT\" FOR APPEND AS 1: PRINT #1, 6: CLOSE 1
30 OPEN \"ZZLOG.DAT\" FOR INPUT AS 1: INPUT #1, A: PRINT A
RUN" \
"56"

run_test "Reading past the end" \
"10 OPEN \"ZZLOG.DAT\" FOR INPUT AS 1
20 INPUT #1, A: GOTO 20
RUN" \
"END OF FILE IN 20"

run_test "File errors" \
"10 CLOSE 1
RUN
10 OPEN \"ZZLOG.DAT\" FOR INPUT AS 2
RUN
10 PRINT #1, 1
RUN
10 OPEN \"A B\" FOR OUTPUT AS 1
RUN" \
"FILE NOT OPEN IN 10
BAD FILE # IN 10
FILE NOT OPEN IN 10
BAD NAME IN 10"

run_test "A program's file is closed when it ends" \
"10 OPEN \"ZZLOG.DAT\" FOR OUTPUT AS 1: PRINT #1, 99
RUN
20 OPEN \"ZZLOG.DAT\" FOR INPUT AS 1: INPUT #1, A: PRINT A
10
RUN" \
"99"
rm -f ZZ*.DAT

# Phase 2 syntax survives LIST, SAVE and LOAD
TOTAL=$((TOTAL + 1))
prog2='10 REM SENSOR LOGGER
20 PINS NET,NET,I2C,I2C
30 FOR I = 1 TO 10 STEP 2: GOSUB 100: NEXT I
40 IF I > 5 AND NOT I = 7 OR I MOD 2 = 0 THEN 60 ELSE PRINT "X";: PRINT
50 OPEN "LOG.DAT" FOR APPEND AS #1: PRINT #1, I; ","; ADC(4): CLOSE #1
60 I2C 72, 1, 2: PRINT I2CR(72, 1), REG(1)
70 IF EOF(1) THEN END
100 RETURN'
second=$(printf "%s\nSAVE zzround\nNEW\nLOAD zzround\nLIST\n" "$prog2" | ./basic 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///' | grep -v '^$')
if [ "$second" = "$prog2" ]; then
    echo -e "${GREEN}✓${NC} Phase 2 syntax: LIST, SAVE and LOAD round trip"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Phase 2 syntax: LIST, SAVE and LOAD round trip"
    diff <(echo "$prog2") <(echo "$second") | head -10
    FAILED=$((FAILED + 1))
fi
rm -f ZZROUND.BAS

# ============================================================
section "Module Files (filesystem on simulated 8KB F-RAM)"
# ============================================================

# basic_fs is the interpreter with the module filesystem, as on LS10
gcc -std=gnu99 -Wall -o basic_fs basic.c fs/fs.c fs/test/basic_fs.c

rm -f ZZFRAM.IMG

run_fs() {
    local test_name="$1"
    local program="$2"
    local expected="$3"
    TOTAL=$((TOTAL + 1))
    actual=$(printf "%s\n" "$program" | ./basic_fs 2>&1 | sed -E 's/^(> )+//' | grep -v "^///" | tr -d '\r' | grep -v '^$')
    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAILED=$((FAILED + 1))
    fi
}

run_fs "Unformatted storage is reported" \
"SAVE x" \
"NOT FORMATTED
NOT FORMATTED"

run_fs "FORMAT YES" \
"FORMAT YES
DIR" \
"NOT FORMATTED"

run_fs "SAVE and DIR" \
"10 PRINT \"HI\"
20 GOTO 10
SAVE prog
DIR" \
"PROG.BAS"

run_fs "Files persist across power cycles" \
"LOAD prog
LIST" \
"10 PRINT \"HI\"
20 GOTO 10"

prog_fs="$prog"
run_fs "Round trip through the filesystem" \
"$prog_fs
SAVE zzround
NEW
LOAD zzround
LIST" \
"$prog_fs"

run_fs "DEL and missing files" \
"DEL zzround
DEL prog
LOAD prog
DEL prog
DIR" \
"NOT FOUND
NOT FOUND"

run_fs "Data file on the module" \
"10 OPEN \"T.DAT\" FOR OUTPUT AS 1: PRINT #1, 12: CLOSE 1
20 OPEN \"T.DAT\" FOR INPUT AS 1: INPUT #1, A: PRINT A
30 CLOSE 1
RUN
DEL T.DAT" \
"12"

run_fs "BOOT.BAS runs at start-up" \
"10 PRINT 1234
SAVE boot" \
""

run_fs "BOOT.BAS output" \
"" \
"1234"

# fill the storage with a large program, then check everything survives
bigprog=""
for i in $(seq 10 10 300); do
    bigprog="$bigprog$i PRINT \"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"
"
done
fill=""
for i in 1 2 3 4 5 6 7 8 9; do fill="${fill}SAVE F$i
"; done
TOTAL=$((TOTAL + 1))
out=$(printf "DEL boot\n%s%s" "$bigprog" "$fill" | ./basic_fs 2>&1 | tr -d '\r' | grep -c "DISK FULL")
again=$(printf "LOAD F1\nLIST\n" | ./basic_fs 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///' | grep -v '^$')
if [ "$out" -ge 1 ] && [ "$again" = "$(printf "%s" "$bigprog")" ]; then
    echo -e "${GREEN}✓${NC} DISK FULL is reported and earlier files are intact"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} DISK FULL is reported and earlier files are intact ($out)"
    FAILED=$((FAILED + 1))
fi

# after deleting everything, the space is available again
TOTAL=$((TOTAL + 1))
out=$(printf "DEL F1\nDEL F2\nDEL F3\nDEL F4\nDEL F5\nDEL F6\nDEL F7\nDEL F8\nDEL F9\nDIR\n%sSAVE G1\nSAVE G2\nSAVE G3\n" "$bigprog" | ./basic_fs 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -E "\.BAS|FULL")
if [ -z "$out" ]; then
    echo -e "${GREEN}✓${NC} Space comes back after deleting"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Space comes back after deleting ($out)"
    FAILED=$((FAILED + 1))
fi


rm -f ZZFRAM.IMG


# ============================================================
section "Machdyne BASIC 1 (docs/basic1.md)"
# ============================================================

run_test "Line numbers 1 to 32767" "1 PRINT 1
32767 PRINT 2
32768 PRINT 3
LIST" "TOO BIG
1 PRINT 1
32767 PRINT 2"
run_test "No immediate mode" "PRINT 1" "SYNTAX ERROR"
run_test "Arithmetic wraps" "10 PRINT 32767 + 1; \" \"; 300 * 300; \" \"; -(-32767 - 1)
RUN" "-32768 24464 -32768"
run_test "Division truncates toward zero" "10 PRINT -7 / 2; \" \"; 7 / -2
RUN" "-3 -3"
run_test "MOD takes the sign of the left operand" "10 PRINT -7 MOD 2; \" \"; 7 MOD -2
RUN" "-1 1"
run_test "-32768 / -1" "10 A = -32767 - 1: PRINT A / -1; \" \"; A MOD -1
RUN" "-32768 0"
run_test "Comparisons do not chain" "10 PRINT 3 > 2 > 1
RUN" "-1
SYNTAX ERROR IN 10"
run_test "NOT binds more loosely than comparisons" "10 PRINT NOT 1 = 2; \" \"; 6 AND 3; \" \"; 1 OR 0 AND 0
RUN" "-1 2 1"
run_test "PRINT items without a separator" "10 PRINT 1 2; -5; 3
RUN" "12-53"
run_test "INPUT reads a sign and digits" "10 INPUT A: INPUT B: INPUT C: PRINT A; \" \"; B; \" \"; C
RUN
12abc
X
40000" "? ? ? 12 0 -25536"
run_test "FOR leaves the last value used" "10 FOR I = 1 TO 3: NEXT: FOR J = 3 TO 1 STEP -1: NEXT: PRINT I; \" \"; J
RUN" "3 1"
run_test "FOR on an active variable replaces its loop" "10 FOR I = 1 TO 2: FOR I = 1 TO 3: NEXT: PRINT \"X\"
RUN" "X"
run_test "RUN clears variables" "10 A = A + 1: PRINT A
RUN
RUN" "1
1"
run_test "REG holds a byte and survives NEW" "10 REG 2, 300: REG 3, -1
RUN
NEW
10 PRINT REG(2); \" \"; REG(3)
RUN" "44 255"
run_test "Eight GOSUBs deep" "10 GOSUB 20
20 GOSUB 30
30 GOSUB 40
40 GOSUB 50
50 GOSUB 60
60 GOSUB 70
70 GOSUB 80
80 GOSUB 90
90 PRINT \"EIGHT\"
RUN" "EIGHT"
run_test "Six FOR loops deep" "10 FOR A=1 TO 1: FOR B=1 TO 1: FOR C=1 TO 1
20 FOR D=1 TO 1: FOR E=1 TO 1: FOR F=1 TO 1
30 PRINT \"SIX\"
40 FOR G=1 TO 1
RUN" "SIX
TOO DEEP IN 40"
run_test "PEEK and POKE are not BASIC 1" "10 POKE 1, 2" "SYNTAX ERROR"
run_test "File names" "10 PRINT 1
SAVE ZZ_A-1
SAVE ZZ!
SAVE ZZTOOLONGN
DIR" "BAD NAME
BAD NAME
ZZ_A-1.BAS"
rm -f ZZ*
run_test "INPUT # is strict" "10 OPEN \"ZZT.DAT\" FOR OUTPUT AS 1: PRINT #1, \"5, x\": CLOSE 1
20 OPEN \"ZZT.DAT\" FOR INPUT AS 1: INPUT #1, A: PRINT A: INPUT #1, B
RUN" "5
SYNTAX ERROR IN 20"
rm -f ZZ*

# ============================================================
section "Extension: more than four pins (PIN), as on Werkzeug"
# ============================================================

gcc -std=gnu99 -Wall -DTARGET_LINUX -DHW_PINS=24 -o basic24 basic.c
run_ext() {
    local test_name="$1" input="$2" expected="$3"
    TOTAL=$((TOTAL + 1))
    actual=$(printf '%s\n' "$input" | timeout 5 ./basic24 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///$' | grep -v '^$')
    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAILED=$((FAILED + 1))
    fi
}
run_ext "PIN is listed canonically" "10 PIN 9, OD: pin 21,ain
LIST" "10 PIN 9,OD: PIN 21,AIN"
run_ext "A declared extra pin works" "10 PIN 9, OD: OUT 9, 0: PRINT IN(9)
RUN" "0"
run_ext "An undeclared extra pin is refused" "10 OUT 9, 1
RUN" "PIN NOT DECLARED IN 10"
run_ext "Pins 1-4 are declared with PINS" "10 PIN 4, OD" "OUT OF RANGE"
run_ext "No pin beyond the target's" "10 PIN 25, OD" "OUT OF RANGE"
run_ext "Extra pins take simple modes only" "10 PIN 9, I2C" "BAD PINS"
run_ext "RUN makes extra pins unused again" "10 PIN 9, OD
RUN
10 OUT 9, 1
RUN" "PIN NOT DECLARED IN 10"
run_ext "PINS still works" "10 PINS -,-,PP,-: OUT 3, 1: PRINT \"OK\"
RUN" "OK"
rm -f basic24

# ============================================================
section "Werkzeug file area for every flash size"
# ============================================================

gcc -std=gnu99 -Wall -o ZZLAYOUT targets/werkzeug/test/layout_test.c
TOTAL=$((TOTAL + 1))
if out=$(./ZZLAYOUT); then
    echo -e "${GREEN}✓${NC} 1MB to 16MB, unknown chips, firmware too large"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} Werkzeug file area: $out"
    FAILED=$((FAILED + 1))
fi
rm -f ZZLAYOUT

# ============================================================
section "Sechs tool (simulated module)"
# ============================================================

gcc -std=gnu99 -Wall -O1 -DSECHS_SIM -DHW_FILES_FS -o sechs_sim tools/sechs/sechs.c basic.c fs/fs.c sechs/sechs.c
rm -f ZZSIM.IMG

run_tool() {
    local test_name="$1" expected="$2"
    shift 2
    TOTAL=$((TOTAL + 1))
    actual=$("$@" 2>&1 | tr -d '\r')
    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAILED=$((FAILED + 1))
    fi
}

run_tool "scan" "0x0c console networked" ./sechs_sim scan
# (the first access is what makes a module "networked")
run_tool "info" "address 0x0c
version 0.5
caps    0x07
status  console
ok      0x1f
fault   0
fw=Machdyne BASIC
mod=SIM
lang=basic" ./sechs_sim info 12

printf '10 PRINT "HI"\nRUN\nSAVE BOOT\n' > ZZSEND.TXT
run_tool "send a program, run it, save it" '10 PRINT "HI"
RUN
HI
SAVE BOOT' ./sechs_sim send 12 ZZSEND.TXT

echo "TYPE BOOT" > ZZSEND.TXT
run_tool "read a file back over I2C" 'TYPE BOOT
10 PRINT "HI"' ./sechs_sim send 0x0c ZZSEND.TXT

echo "LOAD NOFILE" > ZZSEND.TXT
TOTAL=$((TOTAL + 1))
if ! ./sechs_sim send 12 ZZSEND.TXT > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} send fails when the last command fails"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} send fails when the last command fails"
    FAILED=$((FAILED + 1))
fi

run_tool "change the address" "" ./sechs_sim addr 12 0x21
run_tool "the module answers at the new address" "0x21 console networked" ./sechs_sim scan
run_tool "an address out of range is refused" "addresses are 0x08-0x77" ./sechs_sim addr 0x21 0x78
run_tool "no module at the old address" "no Sechs module at 0x0c" ./sechs_sim info 12
rm -f ZZSIM.IMG ZZSEND.TXT

# ============================================================
section "Sechs tool through a USB bridge (simulated)"
# ============================================================

gcc -std=gnu99 -Wall -O1 -o sechsctl tools/sechs/sechs.c
gcc -std=gnu99 -Wall -O1 -DSECHS_SIM -DHW_FILES_FS -o bridge_host tools/sechs/test/bridge_host.c tools/sechs/bridge.c tools/sechs/ch32prog.c basic.c fs/fs.c sechs/sechs.c
rm -f ZZSIM.IMG
./bridge_host > ZZPTY.TXT &
BRIDGE=$!
sleep 0.3
PTY=$(cat ZZPTY.TXT)

run_tool "bridge: scan" "0x0c console networked" ./sechsctl -d "$PTY" scan
printf '10 PRINT "HI"\n20 REG 3, 42\nRUN\nSAVE BOOT\n' > ZZSEND.TXT
run_tool "bridge: send a program" '10 PRINT "HI"
20 REG 3, 42
RUN
HI
SAVE BOOT' ./sechsctl -d "$PTY" send 12 ZZSEND.TXT
run_tool "bridge: a register set by the program" "42" ./sechsctl -d "$PTY" reg 12 3
./sechsctl -d "$PTY" reg 12 5 7
run_tool "bridge: write and read a register" "7" ./sechsctl -d "$PTY" reg 12 5
echo "TYPE BOOT" > ZZSEND.TXT
run_tool "bridge: read a file back" 'TYPE BOOT
10 PRINT "HI"
20 REG 3, 42' ./sechsctl -d "$PTY" send 12 ZZSEND.TXT
run_tool "bridge: INFO" "fw=Machdyne BASIC
mod=SIM
lang=basic" sh -c "./sechsctl -d $PTY info 12 | tail -3"
run_tool "bridge: no module" "no Sechs module at 0x30" ./sechsctl -d "$PTY" info 0x30
head -c 9000 /dev/urandom > ZZFW.BIN; printf 'xx fw=Machdyne BASIC\nmod=LS10A\n' >> ZZFW.BIN
run_tool "bridge: flash a module's firmware (simulated CH32V003)" "ok written and verified" sh -c "./sechsctl -d $PTY flash ZZFW.BIN 2>/dev/null"
head -c 5000 /dev/urandom > ZZFW.BIN
run_tool "bridge: an image that is not Machdyne BASIC is refused" "fail not a Machdyne BASIC firmware (or too large)" sh -c "./sechsctl -d $PTY flash ZZFW.BIN 2>/dev/null"
run_tool "bridge: unless forced" "ok written and verified" sh -c "./sechsctl -d $PTY flash ZZFW.BIN force 2>/dev/null"
rm -f ZZFW.BIN
run_tool "bridge: only 9600 or 115200 baud" "the bridge refused 4800 baud" ./sechsctl -d "$PTY" uart 4800
run_tool "bridge: UART relay (the host bridge echoes)" "hello" sh -c "(printf 'hello\n'; sleep 0.3) | ./sechsctl -d $PTY uart 115200 2>/dev/null"
kill $BRIDGE 2>/dev/null
wait $BRIDGE 2>/dev/null

# the hardware test script, run against a simulated module
rm -f ZZSIM.IMG
./bridge_host > ZZPTY.TXT &
BRIDGE=$!
sleep 0.3
TOTAL=$((TOTAL + 1))
hw=$(SECHSCTL=./sechsctl timeout 120 tools/sechs/hwtest.sh -d "$(cat ZZPTY.TXT)" 2>&1)
if [ $? = 0 ]; then
    echo -e "${GREEN}✓${NC} hwtest.sh passes on a simulated module ($(echo "$hw" | tail -1))"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} hwtest.sh passes on a simulated module"
    echo "$hw" | grep FAIL
    FAILED=$((FAILED + 1))
fi
kill $BRIDGE 2>/dev/null
wait $BRIDGE 2>/dev/null
run_tool "not a bridge" "/dev/null: no Sechs bridge" ./sechsctl -d /dev/null scan
rm -f ZZSIM.IMG ZZSEND.TXT ZZPTY.TXT

# ============================================================
section "Getting-started guide (every program in docs/guide.md)"
# ============================================================

gcc -std=gnu99 -Wall -DTARGET_LINUX -DHW_PINS=24 -o basic24 basic.c
python3 - << 'PYGUIDE' > ZZGUIDE.TXT
import re
text = open("docs/guide.md").read()
for i, block in enumerate(re.findall(r"```basic\n(.*?)```", text, re.S)):
    lines = [l for l in block.strip().split("\n") if l[:1].isdigit()]
    print("%d %s\t%s" % (i, "24" if " PIN " in block else "4", "|".join(lines)))
PYGUIDE
while IFS=$'\t' read -r head lines; do
    n=${head% *}; pins=${head#* }
    bin=./basic; [ "$pins" = 24 ] && bin=./basic24
    want=$(echo "$lines" | tr '|' '\n')
    got=$(printf '%s\nLIST\n' "$want" | timeout 5 $bin 2>&1 | tr -d '\r' | sed -E 's/^(> )+//' | grep -v '^///$' | grep -v '^$')
    TOTAL=$((TOTAL + 1))
    if [ "$got" = "$want" ]; then
        echo -e "${GREEN}✓${NC} guide program $((n + 1)) is valid and listed as printed"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} guide program $((n + 1))"
        echo "  Printed: $want"
        echo "  Got:     $got"
        FAILED=$((FAILED + 1))
    fi
done < ZZGUIDE.TXT
rm -f ZZGUIDE.TXT basic24

# ============================================================
section "Reference sheet (the documents still have what it needs)"
# ============================================================

TOTAL=$((TOTAL + 1))
if out=$(python3 tools/poster/poster.py --html ZZPOSTER.HTML 2>&1) && grep -q "Sechs" ZZPOSTER.HTML; then
    echo -e "${GREEN}✓${NC} poster.py reads docs/basic1.md, targets.md and sechs.md"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} poster.py: $out"
    FAILED=$((FAILED + 1))
fi
rm -f ZZPOSTER.HTML
TOTAL=$((TOTAL + 1))
if out=$(python3 tools/poster/guide.py --html ZZGUIDE.HTML 2>&1) && grep -q "Getting started" ZZGUIDE.HTML; then
    echo -e "${GREEN}✓${NC} guide.py lays out docs/guide.md"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} guide.py: $out"
    FAILED=$((FAILED + 1))
fi
rm -f ZZGUIDE.HTML

# ============================================================

echo ""
echo -e "${YELLOW}================================${NC}"
echo -e "${YELLOW}Test Suite Summary${NC}"
echo -e "${YELLOW}================================${NC}"
echo -e "Total Tests:  $TOTAL"
echo -e "${GREEN}Passed:       $PASSED${NC}"

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed:       $FAILED${NC}"
    echo ""
    echo -e "${RED}TEST SUITE FAILED${NC}"
    exit 1
else
    echo -e "Failed:       $FAILED"
    echo ""
    echo -e "${GREEN}ALL TESTS PASSED ✓${NC}"
    exit 0
fi
