/*
 * Tests for ch32prog against a simulated CH32V003 and CH32V005: a debug module and a
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

static uint8_t img[CH32_FLASH_MAX];

/* a Machdyne BASIC image for a module: LS10A (CH32V003) or LS11A (V005) */
static uint32_t make_image_for(uint32_t len, const char *mod) {
    char id[40];
    for (uint32_t i = 0; i < len; i++) img[i] = (uint8_t)rand();
    snprintf(id, sizeof(id), "fw=Machdyne BASIC\nmod=%s\n", mod);
    memcpy(img + len / 2, id, strlen(id));
    return len;
}

static uint32_t make_image(uint32_t len) { return make_image_for(len, "LS10A"); }

static int flash_is(uint32_t len) {
    for (uint32_t i = 0; i < chip.size; i++)
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
    len = make_image(16384);
    CHECK(run(len, 0) == CH32_OK && flash_is(len), "full 16KB image");
}

/* the CH32V005 (LS11A): 32KB in 256-byte pages */
static void test_v005(void) {
    uint8_t before[CH32_FLASH_MAX];
    chip_new_v005(0x12);
    uint32_t len = make_image_for(20000, "LS11A");
    int r = run(len, 0);
    CHECK(r == CH32_OK, "V005: %s", ch32_message(r));
    CHECK(ch32_chip == 5 && ch32_page == 256, "V005 identified (%d, %lu)", ch32_chip,
          (unsigned long)ch32_page);
    CHECK(flash_is(len), "V005: flash differs");
    CHECK(chip.locked && chip.running && !chip.halted, "V005: locked and running after");
    printf("  V005: %ld debug transactions\n", dmi_n);
    chip_new_v005(0x13);
    len = make_image_for(32768, "LS11A");
    CHECK(run(len, 0) == CH32_OK && flash_is(len), "V005: full 32KB image");

    /* the firmware must be for this chip's module: refused, nothing
     * written, never unlocked, and the chip runs on */
    chip_new_v005(0x14);
    memcpy(before, chip.flash, sizeof(before));
    len = make_image_for(9000, "LS10A");
    key_writes = 0;
    r = run(len, 0);
    CHECK(r == CH32_WRONG_MODULE, "LS10A firmware on a V005: %s", ch32_message(r));
    CHECK(!memcmp(before, chip.flash, chip.size) && key_writes == 0 &&
          chip.running && !chip.halted, "LS10A on V005: untouched, running");
    CHECK(run(len, 1) == CH32_OK && flash_is(len), "LS10A on V005, forced: written");

    chip_new(0x15);
    memcpy(before, chip.flash, 16384);
    len = make_image_for(9000, "LS11A");
    key_writes = 0;
    r = run(len, 0);
    CHECK(r == CH32_WRONG_MODULE, "LS11A firmware on a V003: %s", ch32_message(r));
    CHECK(!memcmp(before, chip.flash, 16384) && key_writes == 0 && chip.running,
          "LS11A on V003: untouched, running");

    /* too large for this chip (but not for a V005): refused, untouched */
    chip_new(0x16);
    memcpy(before, chip.flash, 16384);
    len = make_image(20000);
    CHECK(run(len, 1) == CH32_BAD_IMAGE, "20KB on a V003 refused");
    CHECK(!memcmp(before, chip.flash, 16384) && chip.running, "20KB on V003: untouched");

    /* another chip of the family (CH32V004): refused */
    chip_new_v005(0x17);
    chip.id7f = 0x00400500;
    CHECK(run(make_image_for(4000, "LS11A"), 0) == CH32_WRONG_CHIP, "CH32V004 refused");
    CHECK(violations == 0, "V005: %ld violations", violations);
}

static void test_refusals(void) {
    uint8_t before[CH32_FLASH_MAX];

    chip_new(0x33);
    memcpy(before, chip.flash, sizeof(before));
    uint32_t len = make_image(4000);
    memset(img + len / 2, 'x', 27);                   /* no identity */
    CHECK(run(len, 0) == CH32_BAD_IMAGE && dmi_n == 0, "no identity: refused before touching");
    CHECK(run(len, 1) == CH32_OK && flash_is(len), "no identity, forced: written");
    CHECK(run(0, 1) == CH32_BAD_IMAGE, "empty image refused");
    dmi_n = 0;
    CHECK(ch32_flash(img, CH32_FLASH_MAX + 1, 1, NULL) == CH32_BAD_IMAGE && dmi_n == 0,
          "too large for any chip: refused before touching");

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
static void test_power_cuts(int v005, int points) {
    const char *mod = v005 ? "LS11A" : "LS10A";
    chip_new_as(0x88, v005);
    uint32_t len = make_image_for(5000, mod);
    run(len, 0);
    long total = dmi_n, cuts = 0;
    for (long c = 1; c < total; c += total / points + 1) {
        chip_new_as(0x88 + c, v005);
        make_image_for(len, mod);
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
    printf("  power cuts (%s): %ld points (every %ld transactions), all recovered\n",
           v005 ? "V005" : "V003", cuts, total / points + 1);
}

/* a corrupted bit in one read: never a false success, never a violation */
static void test_corruption(int rounds, int v005) {
    const char *mod = v005 ? "LS11A" : "LS10A";
    uint32_t len = make_image_for(3000, mod);
    int ok = 0, refused = 0;
    long total;
    chip_new_as(0x99, v005);
    run(len, 0);
    total = dmi_n;
    for (int i = 0; i < rounds; i++) {
        chip_new_as(i, v005);
        make_image_for(len, mod);
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
    printf("CH32V003 and CH32V005 programmer tests (simulated chips)\n");
    test_normal();
    test_refusals();
    test_swio_off();
    test_v005();
    test_power_cuts(0, 400);
    test_power_cuts(1, 150);
    test_corruption(300, 0);
    test_corruption(100, 1);
    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("all programmer tests passed\n");
    return 0;
}
