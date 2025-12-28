#!/bin/bash

# BASIC Interpreter Test Suite
# Tests all language features to ensure correctness

set -e  # Exit on first failure

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
    gcc -DTARGET_LINUX -o basic basic.c 2>&1
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
    actual=$(printf "%s\n" "$program" | ./basic 2>&1 | sed 's/^> //g' | sed 's/> //g' | grep -v "^///" | tr -d '\r' | grep -v '^$')
    
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
section "PEEK and POKE"
# ============================================================

run_test "POKE execution" \
"10 POKE 100, 42
RUN" \
" POKE 0x64 <- 0x2a"

run_test "PEEK returns 0 (Linux stub)" \
"10 LET A = PEEK(100)
20 PRINT A
RUN" \
"0"

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
list_output=$(printf "10 PRINT \"Test\"\nLIST\n" | ./basic 2>&1 | sed 's/^> //g' | sed 's/> //g' | grep -v "^///" | tr -d '\r' | grep -v '^$')
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
delete_output=$(printf "10 PRINT \"Keep\"\n20 PRINT \"Delete\"\n20\nLIST\n" | ./basic 2>&1 | sed 's/^> //g' | sed 's/> //g' | grep -v "^///" | tr -d '\r' | grep -v '^$')
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
replace_output=$(printf "10 PRINT \"Old\"\n10 PRINT \"New\"\nLIST\n" | ./basic 2>&1 | sed 's/^> //g' | sed 's/> //g' | grep -v "^///" | tr -d '\r' | grep -v '^$')
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
rm -f test_suite_temp.bas

TOTAL=$((TOTAL + 1))
save_load_output=$(printf "10 PRINT \"Test\"\n20 LET A = 42\nSAVE test_suite_temp.bas\nLOAD test_suite_temp.bas\nLIST\n" | ./basic 2>&1 | sed 's/^> //g' | sed 's/> //g' | grep -v "^///" | grep -v "Saved" | grep -v "Loaded" | tr -d '\r' | grep -v '^$')

if echo "$save_load_output" | grep -q "10 PRINT \"Test\"" && echo "$save_load_output" | grep -q "20 LET A = 42"; then
    echo -e "${GREEN}✓${NC} SAVE and LOAD"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}✗${NC} SAVE and LOAD"
    echo "  Output: $save_load_output"
    FAILED=$((FAILED + 1))
fi

# Clean up
rm -f test_suite_temp.bas

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
