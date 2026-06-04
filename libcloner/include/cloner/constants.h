/**
 * Cloner Constants
 *
 * All magic numbers extracted from the codebase into named defines.
 * Sources: USB packet captures, protocol analysis, and empirical testing.
 */

#ifndef TDFU_CONSTANTS_H
#define TDFU_CONSTANTS_H

/* ------------------------------------------------------------------ */
/* DDR binary format sizes (FIDB + RDD)                                */
/* Source: vendor cloner binary format, verified via reference binaries */
/* ------------------------------------------------------------------ */

#define DDR_FIDB_HEADER_SIZE   8
#define DDR_FIDB_DATA_SIZE_MIN 184 /* T20/T21/T23/T30/T31: no ddr_params */
#define DDR_FIDB_DATA_SIZE_MAX 256 /* A1/T40/T41: ddr_params embedded */
#define DDR_RDD_SIZE           132 /* INNO PHY RDD: 8 header + 124 data */
#define DDR_RDD_REGS_SIZE      204 /* struct ddr_registers RDD: 8 header + 196 data */
#define DDR_RDD_HEADER_SIZE    8
#define DDR_RDD_DATA_SIZE      124
#define DDR_BINARY_SIZE_MAX    512        /* generous max for any platform */
#define DDR_DQ_MAPPING_SIZE    20         /* DQ pin remap table */
#define DDR_PLATFORM_ID        0x19800000 /* Ingenic platform identifier */

/* ------------------------------------------------------------------ */
/* USB endpoints                                                       */
/* Source: USB device descriptor, verified via packet capture           */
/* ------------------------------------------------------------------ */

#define USB_ENDPOINT_OUT 0x01 /* bulk OUT endpoint */
#define USB_ENDPOINT_IN  0x81 /* bulk IN endpoint */

/* ------------------------------------------------------------------ */
/* Transfer sizes                                                      */
/* Source: vendor cloner packet captures                                */
/* ------------------------------------------------------------------ */

#define CHUNK_SIZE_4KB           (4 * 1024)
#define CHUNK_SIZE_64KB          (64 * 1024)
#define CHUNK_SIZE_128KB         (128 * 1024)
#define CHUNK_SIZE_1MB           (1024 * 1024)
#define LARGE_TRANSFER_THRESHOLD (100 * 1024) /* use delays above this */

/* ------------------------------------------------------------------ */
/* Bootstrap timing (milliseconds)                                     */
/* Source: empirical testing, vendor pcap timing analysis               */
/* ------------------------------------------------------------------ */

#define DDR_INIT_WAIT_FAST_MS    1100 /* T20, T41: DDR init wait */
#define DDR_INIT_WAIT_DEFAULT_MS 2000 /* T31 family: standard wait */
#define SPL_POLL_MAX_ATTEMPTS    10   /* max GET_CPU_INFO polls */
#define SPL_POLL_INTERVAL_MS     20   /* between poll attempts */
#define USB_REOPEN_WAIT_MS       5000 /* after A1 USB re-enumerate */
#define UBOOT_TRANSFER_WAIT_MS   500  /* settle after U-Boot send */
#define UBOOT_EXECUTE_WAIT_MS    1000 /* settle after ProgStage2 */
#define INTER_CHUNK_DELAY_MS     50   /* between bulk transfer chunks */

/* ------------------------------------------------------------------ */
/* Protocol timing (milliseconds)                                      */
/* Source: vendor cloner packet captures, empirical                    */
/* ------------------------------------------------------------------ */

#define CMD_RESPONSE_DELAY_MS   100   /* after vendor control transfer */
#define BULK_DATA_PREP_DELAY_MS 50    /* device needs time before bulk */
#define MAX_PROTOCOL_TIMEOUT_MS 60000 /* absolute max for any transfer */
#define DEFAULT_BULK_TIMEOUT_MS 5000  /* standard bulk transfer timeout */

/* ------------------------------------------------------------------ */
/* Firmware write timing                                               */
/* Source: vendor cloner A1/T31 write captures, empirical              */
/* ------------------------------------------------------------------ */

#define ERASE_STATUS_POLL_MS      500   /* poll interval during erase */
#define ERASE_STABLE_POLLS        3     /* consecutive good reads = done */
#define BULK_WRITE_TIMEOUT_MS     6000  /* per-chunk write timeout */
#define METADATA_TIMEOUT_MS       5000  /* flash descriptor transfer */
#define DESCRIPTOR_TIMEOUT_MS     30000 /* long timeout for A1 descriptor */
#define POST_MARKER_DELAY_MS      100   /* after partition marker */
#define HANDSHAKE_BULK_TIMEOUT_MS 10000 /* handshake data transfer */

/* ------------------------------------------------------------------ */
/* Firmware handshake constants                                        */
/* Source: vendor cloner protocol analysis and USB captures            */
/* ------------------------------------------------------------------ */

#define FW_HANDSHAKE_SIZE   40  /* handshake command/response size */
#define FW_LOG_BUFFER_SIZE  512 /* device log drain buffer */
#define MAX_LOG_DRAIN_READS 10  /* max reads to drain device log */
#define CHUNK_OFFSET_SHIFT  16  /* 64KB units in offset encoding */

/* ------------------------------------------------------------------ */
/* Flash descriptor sizes                                              */
/* Source: vendor cloner flash_descriptor format                       */
/* ------------------------------------------------------------------ */

#define FLASH_PARTITION_MARKER_SIZE   172
#define FLASH_DESCRIPTOR_PAYLOAD_SIZE 984

#endif /* TDFU_CONSTANTS_H */
