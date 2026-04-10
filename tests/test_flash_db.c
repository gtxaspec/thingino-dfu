/**
 * Flash Database Tests
 *
 * Tests SPI NOR and NAND flash chip lookup by JEDEC ID and name.
 */

#include <stdio.h>
#include <string.h>
#include "flash/spi_nor_db.h"
#include "flash/nand_db.h"

static int passed = 0;
static int failed = 0;

#define TEST(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

static void test_spi_nor_lookup(void) {
    printf("\nSPI NOR database tests:\n");

    /* Test chip count */
    size_t count;
    const spi_nor_chip_t *chips = spi_nor_list(&count);
    TEST(count >= 49, "SPI NOR chips in database >= 49");
    TEST(chips != NULL, "chip list is non-NULL");

    /* Test lookup by JEDEC ID */
    const spi_nor_chip_t *chip = spi_nor_find_by_id(0xef4018);
    TEST(chip != NULL, "find W25Q128JVSQ by JEDEC ID 0xef4018");
    if (chip) {
        TEST(strcmp(chip->name, "W25Q128JVSQ") == 0, "correct name for 0xef4018");
        TEST(chip->size == 16777216, "correct size 16MB for W25Q128JVSQ");
        TEST(chip->page_size == 256, "correct page size 256 for W25Q128JVSQ");
    }

    /* Test lookup by name */
    chip = spi_nor_find_by_name("GD25Q127CSIG");
    TEST(chip != NULL, "find GD25Q127CSIG by name");
    if (chip) {
        TEST(chip->jedec_id == 0xc84018, "correct JEDEC ID for GD25Q127CSIG");
    }

    /* Test case-insensitive lookup */
    chip = spi_nor_find_by_name("gd25q127csig");
    TEST(chip != NULL, "case-insensitive name lookup works");

    /* Test unknown chip returns NULL */
    chip = spi_nor_find_by_id(0x000000);
    TEST(chip == NULL, "unknown JEDEC ID returns NULL");

    chip = spi_nor_find_by_name("NONEXISTENT_CHIP");
    TEST(chip == NULL, "unknown name returns NULL");

    /* Test a few more known chips */
    chip = spi_nor_find_by_id(0xc84019);
    TEST(chip != NULL, "find chip by JEDEC ID 0xc84019 (GD25Q256)");

    chip = spi_nor_find_by_id(0x2c5b1a);
    TEST(chip != NULL, "find MT35XU512ABA by JEDEC ID 0x2c5b1a");
    if (chip) {
        TEST(chip->size == 67108864, "correct size 64MB for MT35XU512ABA");
    }
}

static void test_nand_lookup(void) {
    printf("\nNAND database tests:\n");

    /* Test chip count */
    size_t count;
    const nand_chip_t *chips = nand_list(&count);
    TEST(count == 12, "12 NAND chips in database");
    TEST(chips != NULL, "chip list is non-NULL");

    /* Test lookup by name */
    const nand_chip_t *chip = nand_find_by_name("HYNIX_H27UBG8T2BTR");
    TEST(chip != NULL, "find HYNIX_H27UBG8T2BTR by name");
    if (chip) {
        TEST(chip->page_size == 8192, "correct page size 8192");
        TEST(chip->oob_size == 640, "correct OOB size 640");
        TEST(chip->chip_id == 0xADD7, "correct chip ID 0xADD7");
    }

    /* Test lookup by ID */
    chip = nand_find_by_id(0xF8F1, 0x00009580);
    TEST(chip != NULL, "find DOS_FMND1G08U3D by chip+ext ID");
    if (chip) {
        TEST(chip->page_size == 2048, "correct page size 2048");
        TEST(chip->block_size == 131072, "correct block size 128KB");
    }

    /* Test unknown returns NULL */
    chip = nand_find_by_id(0x0000, 0x00000000);
    TEST(chip == NULL, "unknown NAND ID returns NULL");

    chip = nand_find_by_name("NONEXISTENT_NAND");
    TEST(chip == NULL, "unknown NAND name returns NULL");
}

int main(void) {
    printf("=== Flash Database Tests ===\n");

    test_spi_nor_lookup();
    test_nand_lookup();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
