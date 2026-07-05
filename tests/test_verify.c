/*
 * Unit tests for the DFU verify read-back comparison (tdfu_first_diff).
 *
 * The verify feature reads flash back after a write and compares it against the
 * source image, reporting the first differing offset. A genuine mismatch can't
 * be induced on healthy hardware (write and verify use the same image, so a
 * good write always matches), so the mismatch/offset logic is validated here.
 *
 * Uses an explicit CHECK macro, not assert(): the project builds with -DNDEBUG,
 * which would compile assert() (and the calls inside it) away to nothing.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdfu/tdfu.h"

/* libtdfu references this app-provided global (see test_file_ops.c). */
bool g_debug_enabled = false;

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL (line %d): %s\n", __LINE__, #cond);                                                         \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_identical(void) {
    uint8_t a[256], b[256];
    for (int i = 0; i < 256; i++)
        a[i] = b[i] = (uint8_t)i;
    CHECK(tdfu_first_diff(a, b, 256) == 256); /* equal -> n */
    CHECK(tdfu_first_diff(a, b, 0) == 0);     /* empty -> 0 (== n) */
    printf("  identical ranges: done\n");
}

static void test_first_byte(void) {
    uint8_t a[64], b[64];
    memset(a, 0xAA, sizeof(a));
    memset(b, 0xAA, sizeof(b));
    b[0] = 0x55;
    CHECK(tdfu_first_diff(a, b, 64) == 0);
    printf("  first-byte diff -> offset 0: done\n");
}

static void test_mid_and_last(void) {
    uint8_t a[1024], b[1024];
    for (int i = 0; i < 1024; i++)
        a[i] = b[i] = (uint8_t)(i * 7);
    b[512] ^= 0xFF;
    CHECK(tdfu_first_diff(a, b, 1024) == 512); /* mid */
    b[512] = a[512];                           /* restore, leave only the last byte differing */
    b[1023] ^= 0x01;
    CHECK(tdfu_first_diff(a, b, 1024) == 1023); /* last */
    printf("  mid/last diff -> correct offsets: done\n");
}

/* Only the first n bytes matter: a difference at/after n must be invisible
 * (mirrors verify stopping at the image length on an oversized flash). */
static void test_bounded_by_n(void) {
    uint8_t a[128], b[128];
    memset(a, 0x00, sizeof(a));
    memset(b, 0x00, sizeof(b));
    b[100] = 0x01;                             /* differs, but beyond the compared window */
    CHECK(tdfu_first_diff(a, b, 100) == 100);  /* window [0,100) is equal */
    CHECK(tdfu_first_diff(a, b, 101) == 100);  /* window includes it -> found */
    printf("  bounded by n (image-length stop): done\n");
}

/* Simulate the verify loop's running offset: a mismatch found in block K at
 * local index i must map to absolute offset total+i (e.g. 0x40000 = env). */
static void test_running_offset(void) {
    const size_t xfer = 4096;
    uint8_t src[4096], flash[4096];
    memset(src, 0x11, xfer);
    memset(flash, 0x11, xfer);
    flash[0x400] = 0x22;            /* byte 0x400 of this block reads back wrong */
    size_t total = 0x40000 - 0x400; /* prior blocks already matched */
    size_t diff = tdfu_first_diff(flash, src, xfer);
    CHECK(diff == 0x400);
    CHECK(total + diff == 0x40000); /* absolute offset the UI reports */
    printf("  running offset math -> 0x%zX: done\n", total + diff);
}

int main(void) {
    printf("test_verify: DFU verify read-back comparison\n");
    test_identical();
    test_first_byte();
    test_mid_and_last();
    test_bounded_by_n();
    test_running_offset();
    if (failures) {
        printf("test_verify: %d CHECK(s) FAILED\n", failures);
        return 1;
    }
    printf("test_verify: ALL PASSED\n");
    return 0;
}
