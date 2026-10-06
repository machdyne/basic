/*
 * A test table for the extension interface (BASIC_EXT, basic.h): one of
 * each kind of statement and function, built into basic_ext for
 * testsuite.sh. Not a real extension.
 */

#include "../basic.h"

static int16_t last;    /* what the last statement received */

static int16_t x_double(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n; (void)e;
    return a[0] * 2;
}

static int16_t x_sum(uint8_t n, const int16_t *a, uint8_t *e) {
    int16_t s = 0;
    (void)e;
    while (n--) s += a[n];
    return s;
}

static int16_t x_seven(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n; (void)a; (void)e;
    return 7;
}

static int16_t x_last(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n; (void)a; (void)e;
    return last;
}

static int16_t x_toggle(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)e;
    last = n * 100 + (n ? a[0] : 0);
    return 0;
}

static int16_t x_plot(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n;
    if (a[0] < 0 || a[0] > 319 || a[1] < 0 || a[1] > 239) *e = BASIC_E_RANGE;
    else last = a[0] + a[1];
    return 0;
}

static int16_t x_orbit(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n; (void)a; (void)e;
    last = 999;
    return 0;
}

static int16_t x_fail(uint8_t n, const int16_t *a, uint8_t *e) {
    (void)n; (void)a;
    *e = BASIC_E_UNSUPPORTED;
    return 0;
}

const basic_ext_t basic_ext[] = {
    { "DOUBLE", 1, 1, 1, x_double },
    { "SUM",    1, 1, 6, x_sum },
    { "SEVEN",  1, 0, 0, x_seven },
    { "LAST",   1, 0, 0, x_last },
    { "TOGGLE", 0, 0, 1, x_toggle },
    { "PLOT",   0, 2, 2, x_plot },
    { "ORBIT",  0, 0, 0, x_orbit },     /* begins with the keyword OR */
    { "FAIL",   0, 0, 0, x_fail },
};
const uint8_t basic_ext_count = sizeof(basic_ext) / sizeof(basic_ext[0]);
