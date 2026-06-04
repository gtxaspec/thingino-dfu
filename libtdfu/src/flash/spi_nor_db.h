/**
 * SPI NOR Flash Chip Database
 *
 * Compiled-in database of supported SPI NOR flash chips.
 * Source: vendor cloner spiflashinfo.cfg (49 chips)
 */

#ifndef SPI_NOR_DB_H
#define SPI_NOR_DB_H

#include <stdint.h>
#include <stddef.h>

/**
 * SPI NOR flash chip database entry
 *
 * All command fields use the vendor cloner's 4-tuple format:
 *   { opcode, dummy_cycles, addr_bytes, mode }
 * where mode encodes data line width (0=single, 5=quad, 6=qpi, 9=dual).
 *
 * Status register config uses 6-tuple:
 *   { cmd, bit, read_en, read_cmd, write_en, reserved }
 * Values of -1 indicate "not supported".
 */
struct spi_nor_chip {
    const char *name;
    uint32_t jedec_id;
    uint32_t size;       /* total size in bytes */
    uint16_t page_size;  /* page program size in bytes */
    uint32_t erase_size; /* sector/block erase size in bytes */

    uint8_t chip_erase_cmd; /* chip erase opcode (0x60 or 0xc7) */
    uint8_t quad_enable;    /* 1 = chip supports quad mode */
    uint8_t addr_mode;      /* 0 = 3-byte, 1 = 4-byte default */

    int32_t timing[5]; /* vendor timing params */

    /* Commands: { opcode, dummy_cycles, addr_bytes, mode } */
    int32_t cmd_read[4];   /* standard read (0x03/0x13) */
    int32_t cmd_qread[4];  /* quad read (0x6b/0x6c) */
    int32_t cmd_write[4];  /* page program (0x02/0x12) */
    int32_t cmd_qwrite[4]; /* quad page program (0x32/0x34) */
    int32_t cmd_erase[4];  /* sector erase (0x52/0x20/0xd8) */
    int32_t cmd_we[4];     /* write enable (0x06) */
    int32_t cmd_en4b[4];   /* enter 4-byte addr mode (0xb7 or -1) */

    /* Status register configs: { cmd, bit, read_en, read_cmd, write_en, reserved } */
    int32_t sr1[6];
    int32_t sr2[6];
    int32_t sr3[6];
};
#ifndef SPI_NOR_CHIP_T_DEFINED
typedef struct spi_nor_chip spi_nor_chip_t;
#endif

/**
 * Find SPI NOR chip by JEDEC ID
 *
 * @param jedec_id  3-byte JEDEC ID (manufacturer + device)
 * @return chip entry or NULL
 */
const spi_nor_chip_t *spi_nor_find_by_id(uint32_t jedec_id);

/**
 * Find SPI NOR chip by name
 *
 * @param name  chip name (case-insensitive)
 * @return chip entry or NULL
 */
const spi_nor_chip_t *spi_nor_find_by_name(const char *name);

/**
 * List all SPI NOR chips
 *
 * @param count  output: number of chips
 * @return array of chip entries
 */
const spi_nor_chip_t *spi_nor_list(size_t *count);

#endif /* SPI_NOR_DB_H */
