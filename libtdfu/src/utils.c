#include "tdfu/tdfu.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// LOG HOOK
// ============================================================================

tdfu_log_fn g_tdfu_log_hook = NULL;

void tdfu_log_output(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0)
        return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    if (g_tdfu_log_hook) {
        g_tdfu_log_hook(buf, len);
    } else {
        fwrite(buf, 1, len, stderr);
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char *tdfu_variant_to_string(tdfu_variant_t variant) {
    switch (variant) {
    case TDFU_VARIANT_T10:
        return "t10";
    case TDFU_VARIANT_T20:
        return "t20";
    case TDFU_VARIANT_T21:
        return "t21";
    case TDFU_VARIANT_T23:
        return "t23";
    case TDFU_VARIANT_T23DL:
        return "t23dl";
    case TDFU_VARIANT_T30:
        return "t30";
    case TDFU_VARIANT_T31:
        return "t31";
    case TDFU_VARIANT_T31X:
        return "t31x";
    case TDFU_VARIANT_T31ZX:
        return "t31zx";
    case TDFU_VARIANT_T31A:
        return "t31a";
    case TDFU_VARIANT_A1:
        return "a1";
    case TDFU_VARIANT_T40:
        return "t40";
    case TDFU_VARIANT_T41:
        return "t41";
    case TDFU_VARIANT_T32:
        return "t32";
    case TDFU_VARIANT_T32_DDR3:
        return "t32_ddr3";
    case TDFU_VARIANT_X1000:
        return "x1000";
    case TDFU_VARIANT_X1600:
        return "x1600";
    case TDFU_VARIANT_X1700:
        return "x1700";
    case TDFU_VARIANT_X2000:
        return "x2000";
    case TDFU_VARIANT_X2100:
        return "x2100";
    case TDFU_VARIANT_X2600:
        return "x2600";
    case TDFU_VARIANT_T31AL:
        return "t31al";
    case TDFU_VARIANT_T40XP:
        return "t40xp";
    case TDFU_VARIANT_T41_DDR3:
        return "t41_ddr3";
    case TDFU_VARIANT_T41N:
        return "t41n";
    case TDFU_VARIANT_T41NQ:
        return "t41nq";
    case TDFU_VARIANT_T41L:
        return "t41l";
    case TDFU_VARIANT_T41LQ:
        return "t41lq";
    case TDFU_VARIANT_T41A:
        return "t41a";
    case TDFU_VARIANT_T41ZL:
        return "t41zl";
    case TDFU_VARIANT_T41ZX:
        return "t41zx";
    /* Per-variant loaders (1:1 with firmware/dfu/<dir>). */
    case TDFU_VARIANT_T10N: return "t10n";
    case TDFU_VARIANT_T10L: return "t10l";
    case TDFU_VARIANT_T20N: return "t20n";
    case TDFU_VARIANT_T20X: return "t20x";
    case TDFU_VARIANT_T20L: return "t20l";
    case TDFU_VARIANT_T21N: return "t21n";
    case TDFU_VARIANT_T21HP: return "t21hp";
    case TDFU_VARIANT_T23N: return "t23n";
    case TDFU_VARIANT_T23DN: return "t23dn";
    case TDFU_VARIANT_T23NHP: return "t23nhp";
    case TDFU_VARIANT_T23NLP: return "t23nlp";
    case TDFU_VARIANT_T23X: return "t23x";
    case TDFU_VARIANT_T23ZN: return "t23zn";
    case TDFU_VARIANT_T30N: return "t30n";
    case TDFU_VARIANT_T30X: return "t30x";
    case TDFU_VARIANT_T30L: return "t30l";
    case TDFU_VARIANT_T30A: return "t30a";
    case TDFU_VARIANT_T31N: return "t31n";
    case TDFU_VARIANT_T31L: return "t31l";
    case TDFU_VARIANT_T32LQ: return "t32lq";
    case TDFU_VARIANT_T32NQ: return "t32nq";
    case TDFU_VARIANT_T32VN: return "t32vn";
    case TDFU_VARIANT_T32VNP: return "t32vnp";
    case TDFU_VARIANT_T32VX: return "t32vx";
    case TDFU_VARIANT_T32XQ: return "t32xq";
    case TDFU_VARIANT_T33: return "t33";
    case TDFU_VARIANT_T40N: return "t40n";
    case TDFU_VARIANT_A1N: return "a1n";
    default:
        return "unknown";
    }
}

tdfu_variant_t tdfu_variant_from_string(const char *str) {
    if (!str)
        return TDFU_VARIANT_T31X;

    // Convert to lowercase for case-insensitive comparison
    char lower[32] = {0};
    for (int i = 0; str[i] && i < 31; i++) {
        lower[i] = tolower(str[i]);
    }

    if (strcmp(lower, "a1") == 0)
        return TDFU_VARIANT_A1;
    if (strcmp(lower, "t10") == 0)
        return TDFU_VARIANT_T10;
    if (strcmp(lower, "t20") == 0)
        return TDFU_VARIANT_T20;
    if (strcmp(lower, "t21") == 0)
        return TDFU_VARIANT_T21;
    if (strcmp(lower, "t23") == 0)
        return TDFU_VARIANT_T23;
    if (strcmp(lower, "t23dl") == 0)
        return TDFU_VARIANT_T23DL;
    if (strcmp(lower, "t30") == 0)
        return TDFU_VARIANT_T30;
    if (strcmp(lower, "t31") == 0)
        return TDFU_VARIANT_T31;
    if (strcmp(lower, "t31x") == 0)
        return TDFU_VARIANT_T31X;
    if (strcmp(lower, "t31zx") == 0)
        return TDFU_VARIANT_T31ZX;
    if (strcmp(lower, "t31a") == 0)
        return TDFU_VARIANT_T31A;
    if (strcmp(lower, "t31al") == 0)
        return TDFU_VARIANT_T31AL;
    if (strcmp(lower, "c100") == 0)
        return TDFU_VARIANT_T31A;
    if (strcmp(lower, "t40") == 0)
        return TDFU_VARIANT_T40;
    if (strcmp(lower, "t40xp") == 0)
        return TDFU_VARIANT_T40XP;
    if (strcmp(lower, "t41") == 0)
        return TDFU_VARIANT_T41;
    if (strcmp(lower, "t41_ddr3") == 0)
        return TDFU_VARIANT_T41_DDR3;
    /* T41 per-chip overrides (display the exact model; firmware via dfu_variant_dir). */
    if (strcmp(lower, "t41l") == 0)
        return TDFU_VARIANT_T41L;
    if (strcmp(lower, "t41lq") == 0)
        return TDFU_VARIANT_T41LQ;
    if (strcmp(lower, "t41n") == 0)
        return TDFU_VARIANT_T41N;
    if (strcmp(lower, "t41nq") == 0)
        return TDFU_VARIANT_T41NQ;
    if (strcmp(lower, "t41a") == 0)
        return TDFU_VARIANT_T41A;
    if (strcmp(lower, "t41zl") == 0)
        return TDFU_VARIANT_T41ZL;
    if (strcmp(lower, "t41zx") == 0)
        return TDFU_VARIANT_T41ZX;
    if (strcmp(lower, "t41zn") == 0)
        return TDFU_VARIANT_T41_DDR3;
    if (strcmp(lower, "t40nn") == 0)
        return TDFU_VARIANT_T40N;
    if (strcmp(lower, "t32") == 0)
        return TDFU_VARIANT_T32;
    if (strcmp(lower, "t32_ddr3") == 0)
        return TDFU_VARIANT_T32_DDR3;
    if (strcmp(lower, "x1000") == 0)
        return TDFU_VARIANT_X1000;
    if (strcmp(lower, "x1600") == 0)
        return TDFU_VARIANT_X1600;
    if (strcmp(lower, "x1700") == 0)
        return TDFU_VARIANT_X1700;
    if (strcmp(lower, "x2000") == 0)
        return TDFU_VARIANT_X2000;
    if (strcmp(lower, "x2100") == 0)
        return TDFU_VARIANT_X2100;
    if (strcmp(lower, "x2600") == 0)
        return TDFU_VARIANT_X2600;
    /* Per-variant loaders (--cpu / web selection -> the exact loader). */
    if (strcmp(lower, "t10n") == 0) return TDFU_VARIANT_T10N;
    if (strcmp(lower, "t10l") == 0) return TDFU_VARIANT_T10L;
    if (strcmp(lower, "t20n") == 0) return TDFU_VARIANT_T20N;
    if (strcmp(lower, "t20x") == 0) return TDFU_VARIANT_T20X;
    if (strcmp(lower, "t20l") == 0) return TDFU_VARIANT_T20L;
    if (strcmp(lower, "t21n") == 0) return TDFU_VARIANT_T21N;
    if (strcmp(lower, "t21hp") == 0) return TDFU_VARIANT_T21HP;
    if (strcmp(lower, "t23n") == 0) return TDFU_VARIANT_T23N;
    if (strcmp(lower, "t23dn") == 0) return TDFU_VARIANT_T23DN;
    if (strcmp(lower, "t23nhp") == 0) return TDFU_VARIANT_T23NHP;
    if (strcmp(lower, "t23nlp") == 0) return TDFU_VARIANT_T23NLP;
    if (strcmp(lower, "t23x") == 0) return TDFU_VARIANT_T23X;
    if (strcmp(lower, "t23zn") == 0) return TDFU_VARIANT_T23ZN;
    if (strcmp(lower, "t30n") == 0) return TDFU_VARIANT_T30N;
    if (strcmp(lower, "t30x") == 0) return TDFU_VARIANT_T30X;
    if (strcmp(lower, "t30l") == 0) return TDFU_VARIANT_T30L;
    if (strcmp(lower, "t30a") == 0) return TDFU_VARIANT_T30A;
    if (strcmp(lower, "t31n") == 0) return TDFU_VARIANT_T31N;
    if (strcmp(lower, "t31l") == 0) return TDFU_VARIANT_T31L;
    if (strcmp(lower, "t32lq") == 0) return TDFU_VARIANT_T32LQ;
    if (strcmp(lower, "t32nq") == 0) return TDFU_VARIANT_T32NQ;
    if (strcmp(lower, "t32vn") == 0) return TDFU_VARIANT_T32VN;
    if (strcmp(lower, "t32vnp") == 0) return TDFU_VARIANT_T32VNP;
    if (strcmp(lower, "t32vx") == 0) return TDFU_VARIANT_T32VX;
    if (strcmp(lower, "t32xq") == 0) return TDFU_VARIANT_T32XQ;
    if (strcmp(lower, "t33") == 0) return TDFU_VARIANT_T33;
    if (strcmp(lower, "t40n") == 0) return TDFU_VARIANT_T40N;
    if (strcmp(lower, "a1n") == 0) return TDFU_VARIANT_A1N;

    // Default to T31X if unknown
    return TDFU_VARIANT_T31X;
}

const char *tdfu_stage_to_string(tdfu_stage_t stage) {
    switch (stage) {
    case TDFU_STAGE_BOOTROM:
        return "bootrom";
    case TDFU_STAGE_FIRMWARE:
        return "firmware";
    default:
        return "unknown";
    }
}

const char *tdfu_error_to_string(tdfu_error_t error) {
    switch (error) {
    case TDFU_SUCCESS:
        return "Success";
    case TDFU_ERROR_INIT_FAILED:
        return "Initialization failed";
    case TDFU_ERROR_DEVICE_NOT_FOUND:
        return "Device not found";
    case TDFU_ERROR_OPEN_FAILED:
        return "Failed to open device";
    case TDFU_ERROR_TRANSFER_FAILED:
        return "Transfer failed";
    case TDFU_ERROR_TIMEOUT:
        return "Timeout";
    case TDFU_ERROR_INVALID_PARAMETER:
        return "Invalid parameter";
    case TDFU_ERROR_MEMORY:
        return "Memory allocation failed";
    case TDFU_ERROR_FILE_IO:
        return "File I/O error";
    case TDFU_ERROR_PROTOCOL:
        return "Protocol error";
    case TDFU_ERROR_TRANSFER_TIMEOUT:
        return "Transfer timeout";
    default:
        return "Unknown error";
    }
}

tdfu_variant_t detect_variant_from_magic(const char *magic) {
    if (!magic) {
        return TDFU_VARIANT_T31X;
    }

    DEBUG_PRINT("detect_variant_from_magic: input='%s' (length=%zu)\n", magic, magic ? strlen(magic) : 0);

    // X2580 is reported by multiple SoCs in firmware stage (T32, T41, etc.)
    // and cannot be used to determine the variant. Return T31X as fallback
    // and rely on auto-detect or --cpu for correct identification.
    if (strstr(magic, "X2580") || strstr(magic, "x2580")) {
        DEBUG_PRINT("detect_variant_from_magic: X2580 is ambiguous (T32/T41), using fallback\n");
        return TDFU_VARIANT_T31X;
    }

    // Check for X-series processors first (more specific)
    if (strstr(magic, "x1000") || strstr(magic, "X1000"))
        return TDFU_VARIANT_X1000;
    if (strstr(magic, "x1600") || strstr(magic, "X1600"))
        return TDFU_VARIANT_X1600;
    if (strstr(magic, "x1700") || strstr(magic, "X1700"))
        return TDFU_VARIANT_X1700;
    if (strstr(magic, "x2000") || strstr(magic, "X2000"))
        return TDFU_VARIANT_X2000;
    if (strstr(magic, "x2100") || strstr(magic, "X2100"))
        return TDFU_VARIANT_X2100;
    if (strstr(magic, "x2600") || strstr(magic, "X2600"))
        return TDFU_VARIANT_X2600;

    // Check for A1 (special case - reports "A1" in firmware stage)
    if (strcmp(magic, "A1") == 0 || strcmp(magic, "a1") == 0) {
        DEBUG_PRINT("detect_variant_from_magic: matched A1 -> A1\n");
        return TDFU_VARIANT_A1;
    }

    // Check for T31 sub-variants
    if (strstr(magic, "t31zx") || strstr(magic, "T31ZX") || strstr(magic, "zx"))
        return TDFU_VARIANT_T31ZX;

    // Parse common patterns from Ingenic CPUs
    // Format is typically "BOOT47XX" where XX indicates processor variant
    // But we're getting "T 3 1 V " format (with spaces), so handle that too
    if (strlen(magic) >= 4) {
        DEBUG_PRINT("detect_variant_from_magic: checking pattern match\n");

        // Create a compact version without spaces for comparison
        char compact_magic[9] = {0};
        int compact_pos = 0;
        for (int i = 0; magic[i] && compact_pos < 8; i++) {
            if (magic[i] != ' ') {
                compact_magic[compact_pos++] = magic[i];
            }
        }

        // Check for T31 pattern at the beginning
        if (strncmp(compact_magic, "T31V", 4) == 0) {
            DEBUG_PRINT("detect_variant_from_magic: matched T31V -> T31ZX\n");
            return TDFU_VARIANT_T31ZX; // T31V indicates T31ZX
        }
        if (strncmp(compact_magic, "T31", 3) == 0) {
            DEBUG_PRINT("detect_variant_from_magic: matched T31 -> T31\n");
            return TDFU_VARIANT_T31;
        }
        if (strncmp(compact_magic, "T5V", 3) == 0)
            return TDFU_VARIANT_T10; /* T10 reports "T5V1" */
        if (strncmp(compact_magic, "T10", 3) == 0)
            return TDFU_VARIANT_T10;
        if (strncmp(compact_magic, "T20", 3) == 0)
            return TDFU_VARIANT_T20;
        if (strncmp(compact_magic, "T21", 3) == 0)
            return TDFU_VARIANT_T21;
        if (strncmp(compact_magic, "T23", 3) == 0)
            return TDFU_VARIANT_T23;
        if (strncmp(compact_magic, "T30", 3) == 0)
            return TDFU_VARIANT_T30;
        if (strncmp(compact_magic, "T40", 3) == 0)
            return TDFU_VARIANT_T40;
        if (strncmp(compact_magic, "T41", 3) == 0)
            return TDFU_VARIANT_T41;
    }

    // Fallback to original pattern for 8-character strings
    if (strlen(magic) >= 8) {
        const char *suffix = &magic[6];

        if (strncmp(suffix, "20", 2) == 0)
            return TDFU_VARIANT_T20;
        if (strncmp(suffix, "21", 2) == 0)
            return TDFU_VARIANT_T21;
        if (strncmp(suffix, "23", 2) == 0)
            return TDFU_VARIANT_T23;
        if (strncmp(suffix, "30", 2) == 0)
            return TDFU_VARIANT_T30;
        if (strncmp(suffix, "31", 2) == 0)
            return TDFU_VARIANT_T31;
        if (strncmp(suffix, "40", 2) == 0)
            return TDFU_VARIANT_T40;
        if (strncmp(suffix, "41", 2) == 0)
            return TDFU_VARIANT_T41;
    }

    DEBUG_PRINT("detect_variant_from_magic: defaulting to T31X\n");
    return TDFU_VARIANT_T31X; // Default to T31X
}

// ============================================================================
// FILE I/O HELPERS
// ============================================================================

tdfu_error_t load_file(const char *filename, uint8_t **data, size_t *size) {
    if (!filename || !data || !size)
        return TDFU_ERROR_INVALID_PARAMETER;

    FILE *file = fopen(filename, "rb");
    if (!file)
        return TDFU_ERROR_FILE_IO;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }

    *data = (uint8_t *)malloc(file_size);
    if (!*data) {
        fclose(file);
        return TDFU_ERROR_MEMORY;
    }

    size_t bytes_read = fread(*data, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(*data);
        *data = NULL;
        return TDFU_ERROR_FILE_IO;
    }

    *size = bytes_read;
    return TDFU_SUCCESS;
}

tdfu_error_t firmware_file_check_readable(const char *path) {
    if (!path)
        return TDFU_ERROR_INVALID_PARAMETER;

    FILE *f = fopen(path, "rb");
    if (!f)
        return TDFU_ERROR_FILE_IO;

    fclose(f);
    return TDFU_SUCCESS;
}