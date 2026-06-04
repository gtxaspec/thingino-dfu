/**
 * NAND Flash Chip Database
 *
 * Compiled-in database of supported NAND flash chips.
 * Source: vendor cloner nandinfo.cfg (12 chips)
 */

#ifndef NAND_DB_H
#define NAND_DB_H

#include <stdint.h>
#include <stddef.h>

/**
 * NAND flash chip database entry
 *
 * Field layout matches the vendor cloner's nandinfo.cfg CSV format.
 * Timing values are in nanoseconds unless noted otherwise.
 */
typedef struct {
    uint16_t chip_id;    /* 2-byte NAND chip ID */
    uint32_t ext_id;     /* 4-byte extended ID */
    const char *name;    /* chip model name */
    uint32_t page_size;  /* page size in bytes */
    uint32_t block_size; /* block size in bytes */
    uint16_t oob_size;   /* OOB/spare bytes per page */

    /* Geometry */
    uint8_t row_cycles; /* row address cycles */
    uint8_t col_cycles; /* column address cycles */
    uint8_t planes;     /* planes per die */
    uint8_t blocks_exp; /* blocks per plane (log2 or raw) */
    uint8_t dies;       /* dies per package */
    uint32_t buf_size;  /* internal buffer size */

    int32_t flags[7];     /* chip capability flags */
    int32_t timing[13];   /* primary timing parameters (ns) */
    int32_t spare_config; /* spare area configuration */

    /* ECC configuration */
    uint8_t ecc_strength; /* required ECC strength (bits) */
    uint16_t ecc_step;    /* ECC step size */
    uint16_t ecc_page;    /* ECC page size */
    uint8_t ecc_reserved;

    int32_t timing2[4]; /* additional timing parameters */
} nand_chip_t;

/**
 * Find NAND chip by chip ID and extended ID
 *
 * @param chip_id  2-byte chip ID
 * @param ext_id   4-byte extended ID
 * @return chip entry or NULL
 */
const nand_chip_t *nand_find_by_id(uint16_t chip_id, uint32_t ext_id);

/**
 * Find NAND chip by name
 *
 * @param name  chip name (case-insensitive)
 * @return chip entry or NULL
 */
const nand_chip_t *nand_find_by_name(const char *name);

/**
 * List all NAND chips
 *
 * @param count  output: number of chips
 * @return array of chip entries
 */
const nand_chip_t *nand_list(size_t *count);

#endif /* NAND_DB_H */
