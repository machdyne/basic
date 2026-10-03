/*
 * Tests for ch32prog against a simulated CH32V003: a debug module and a
 * flash controller that are stricter than the real chip. Anything the
 * failsafe rules forbid (docs/ch32prog.md) is counted as a violation, and
 * every test requires zero violations.
 *
 *   cc -o ch32prog_test ch32prog_test.c ../ch32prog.c && ./ch32prog_test
 */

#include "ch32sim.h"

#include <stdio.h>
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

/* ---- tests ------------------------------------------------------------------- */

static uint8_t img[CH32_FLASH_SIZE];

static uint32_t make_image(uint32_t len) {
    for (uint32_t i = 0; i < len; i++) img[i] = (uint8_t)rand();
    memcpy(img + len / 2, "fw=Machdyne BASIC", 17);
    return len;
}

static int flash_is(uint32_t len) {
    for (uint32_t i = 0; i < CH32_FLASH_SIZE; i++)
        if (chip.flash[i] != (i < len ? img[i] : 0xFF)) return 0;
    return 1;
}

static int run(uint32_t len, int force) {
    dmi_n = 0;
    return ch32_flash(img, len, force, NULL);
}

static void test_normal(void) {
    chip_new(0x11);
    uint32_t len = make_image(12000);
    int r = run(len, 0);
    CHECK(r == CH32_OK, "normal: %s", ch32_message(r));
    CHECK(flash_is(len), "normal: flash differs");
    CHECK(chip.locked && chip.running && !chip.halted, "normal: locked and running after");
    CHECK(violations == 0, "normal: %ld violations", violations);
    printf("  normal: %ld debug transactions\n", dmi_n);

    chip_new(0x22);
    len = make_image(CH32_FLASH_SIZE);
    CHECK(run(len, 0) == CH32_OK && flash_is(len), "full 16KB image");
}

static void test_refusals(void) {
    uint8_t before[CH32_FLASH_SIZE];

    chip_new(0x33);
    memcpy(before, chip.flash, sizeof(before));
    uint32_t len = make_image(4000);
    memset(img + len / 2, 'x', 17);                   /* no identity */
    CHECK(run(len, 0) == CH32_BAD_IMAGE && dmi_n == 0, "no identity: refused before touching");
    CHECK(run(len, 1) == CH32_OK && flash_is(len), "no identity, forced: written");
    CHECK(run(0, 1) == CH32_BAD_IMAGE, "empty image refused");
    CHECK(ch32_flash(img, CH32_FLASH_SIZE + 1, 1, NULL) == CH32_BAD_IMAGE, "too large refused");

    chip_new(0x44);
    memcpy(before, chip.flash, sizeof(before));
    len = make_image(4000);
    chip.absent = 1;
    CHECK(run(len, 0) == CH32_NO_CHIP, "no chip");

    chip_new(0x55);
    chip.chip_id = 0x00400600;                        /* not a CH32V003 */
    key_writes = 0;
    CHECK(run(len, 0) == CH32_WRONG_CHIP, "wrong chip refused");
    CHECK(key_writes == 0 && !memcmp(before, chip.flash, 0) , "wrong chip: no unlock");

    chip_new(0x66);
    memcpy(before, chip.flash, sizeof(before));
    chip.rdprt = 1;
    key_writes = 0;
    CHECK(run(len, 0) == CH32_LOCKED, "read-protected refused");
    CHECK(key_writes == 0 && !memcmp(before, chip.flash, sizeof(before)),
          "read-protected: flash untouched, never unlocked");
    CHECK(violations == 0, "refusals: %ld violations", violations);
}

/* firmware that turns SWIO off: recoverable with RESETN, not without */
static void test_swio_off(void) {
    chip_new(0x77);
    chip.swio_off_fw = 1;
    uint32_t len = make_image(6000);
    chip.boot_left = 0;             /* it has been running for a while */
    chip.reset_wired = 0;
    CHECK(run(len, 0) == CH32_NO_CHIP, "SWIO off, no RESETN: cannot reach it");
    chip.reset_wired = 1;
    chip_power();
    chip.boot_left = 0;
    int r = run(len, 0);
    CHECK(r == CH32_OK && flash_is(len), "SWIO off, with RESETN: recovered (%s)", ch32_message(r));
    CHECK(violations == 0, "SWIO off: %ld violations", violations);
}

/* a power cut at every Nth transaction, then programming again */
static void test_power_cuts(void) {
    chip_new(0x88);
    uint32_t len = make_image(5000);
    run(len, 0);
    long total = dmi_n, cuts = 0;
    for (long c = 1; c < total; c += total / 400 + 1) {
        chip_new(0x88 + c);
        make_image(len);
        cut_at = c;
        dmi_n = 0;
        if (!setjmp(cut)) ch32_flash(img, len, 0, NULL);
        cut_at = -1;
        chip_power();                               /* power comes back */
        int r = run(len, 0);
        CHECK(r == CH32_OK && flash_is(len), "cut at %ld: then %s", c, ch32_message(r));
        cuts++;
    }
    CHECK(violations == 0, "power cuts: %ld violations", violations);
    printf("  power cuts: %ld points (every %ld transactions), all recovered\n",
           cuts, total / 400 + 1);
}

/* a corrupted bit in one read: never a false success, never a violation */
static void test_corruption(int rounds) {
    uint32_t len = make_image(3000);
    int ok = 0, refused = 0;
    long total;
    chip_new(0x99);
    run(len, 0);
    total = dmi_n;
    for (int i = 0; i < rounds; i++) {
        chip_new(i);
        make_image(len);
        corrupt_at = 1 + rand() % total;
        int r = run(len, 0);
        corrupt_at = -1;
        if (r == CH32_OK) {
            ok++;
            CHECK(flash_is(len), "corruption at %ld: success reported, flash wrong", corrupt_at);
        } else {
            refused++;
            chip_power();
            int r2 = run(len, 0);
            CHECK(r2 == CH32_OK && flash_is(len), "after a failure (%s), a clean run: %s",
                  ch32_message(r), ch32_message(r2));
        }
    }
    CHECK(violations == 0, "corruption: %ld violations", violations);
    CHECK(ch32_refused == 0, "the write gate refused something");
    printf("  corrupted reads: %d rounds, %d succeeded anyway, %d stopped safely\n",
           rounds, ok, refused);
}

int main(void) {
    srand(4242);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CH32V003 programmer tests (simulated chip)\n");
    test_normal();
    test_refusals();
    test_swio_off();
    test_power_cuts();
    test_corruption(300);
    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("all programmer tests passed\n");
    return 0;
}
