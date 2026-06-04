/**
 * DDR Reference Binary Comparison Test
 *
 * Validates that DDR binaries generated from the chip database match
 * the vendor reference binaries byte-for-byte. Reference binaries
 * are loaded from firmware/cloner/<platform>/ddr.bin files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ddr/ddr_binary_builder.h"
#include "ddr/ddr_config_database.h"
#include "thingino.h"

bool g_debug_enabled = false;

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

static uint8_t *load_ref_binary(const char *path, size_t *size) {
    uint8_t *data = NULL;
    if (load_file(path, &data, size) != TDFU_SUCCESS) {
        /* Try with ../ prefix for running from build dir */
        char alt[512];
        snprintf(alt, sizeof(alt), "../%s", path);
        if (load_file(alt, &data, size) != TDFU_SUCCESS)
            return NULL;
    }
    return data;
}

static void compare_section(const char *label, const uint8_t *expected,
                            const uint8_t *actual, size_t offset, size_t len) {
    int mismatches = 0;
    for (size_t i = 0; i < len; i++) {
        if (expected[offset + i] != actual[offset + i]) {
            if (mismatches == 0)
                printf("    [MISMATCH] %s at offset 0x%03zx:\n", label, offset + i);
            if (mismatches < 10)
                printf("      [0x%03zx] expected=0x%02x got=0x%02x\n",
                       offset + i, expected[offset + i], actual[offset + i]);
            mismatches++;
        }
    }
    if (mismatches == 0)
        printf("    [OK] %s (%zu bytes match)\n", label, len);
    else
        printf("    [FAIL] %s: %d mismatches in %zu bytes\n", label, mismatches, len);
}

static int test_fidb_section(const char *proc_name, const char *ddr_file) {
    printf("\n  FIDB section test: %s\n", proc_name);

    size_t ref_size;
    uint8_t *ref_binary = load_ref_binary(ddr_file, &ref_size);
    if (!ref_binary) {
        printf("    [SKIP] Cannot load %s\n", ddr_file);
        tests_skipped++;
        return 0;
    }

    platform_config_t platform;
    if (ddr_get_platform_config(proc_name, &platform) != 0) {
        printf("    [SKIP] No platform config for %s\n", proc_name);
        tests_skipped++;
        free(ref_binary);
        return 0;
    }

    uint8_t fidb[192];
    memset(fidb, 0, sizeof(fidb));
    size_t fidb_size = ddr_build_fidb(&platform, fidb);

    if (fidb_size == 0) {
        printf("    [FAIL] ddr_build_fidb returned 0\n");
        tests_failed++;
        free(ref_binary);
        return -1;
    }

    compare_section("FIDB header", ref_binary, fidb, 0, 8);
    compare_section("FIDB platform config", ref_binary, fidb, 8, fidb_size - 8);

    size_t cmp_len = (fidb_size < ref_size) ? fidb_size : ref_size;
    if (cmp_len > 192) cmp_len = 192;
    if (memcmp(ref_binary, fidb, cmp_len) == 0) {
        printf("    [PASS] FIDB matches reference (%zu bytes)\n", cmp_len);
        tests_passed++;
    } else {
        printf("    [FAIL] FIDB does not match reference\n");
        tests_failed++;
    }

    free(ref_binary);
    return 0;
}

static void test_database_lookup(void) {
    printf("\n  Database lookup tests:\n");

    size_t proc_count;
    const processor_config_t *procs = processor_config_list(&proc_count);

    int lookup_pass = 1;
    for (size_t i = 0; i < proc_count; i++) {
        const ddr_chip_config_t *ddr = ddr_chip_config_get_default(procs[i].name);
        if (!ddr) {
            printf("    [FAIL] No default DDR for processor: %s\n", procs[i].name);
            lookup_pass = 0;
        }
    }
    if (lookup_pass) {
        printf("    [PASS] All %zu processors have default DDR mappings\n", proc_count);
        tests_passed++;
    } else {
        tests_failed++;
    }

    size_t ddr_count;
    ddr_chip_config_list(&ddr_count);
    printf("    [INFO] DDR database has %zu chips\n", ddr_count);
    if (ddr_count >= 30) {
        printf("    [PASS] DDR chip count >= 30\n");
        tests_passed++;
    } else {
        printf("    [FAIL] DDR chip count too low: %zu\n", ddr_count);
        tests_failed++;
    }
}

int main(void) {
    printf("=== DDR Reference Binary Comparison Tests ===\n");

    test_database_lookup();

    test_fidb_section("t20", "firmware/cloner/t20/ddr.bin");
    test_fidb_section("t31", "firmware/cloner/t31x/ddr.bin");
    test_fidb_section("a1", "firmware/cloner/a1_n_ne_x/ddr.bin");

    printf("\n=== Results: %d passed, %d failed, %d skipped ===\n",
           tests_passed, tests_failed, tests_skipped);

    return tests_failed > 0 ? 1 : 0;
}
