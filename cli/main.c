#include "thingino.h"
#include "cloner/core.h"
#include "cloner/protocol.h"
#include "remote.h"
#include "ddr_config_database.h"
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <libgen.h>
#include <limits.h>
#endif

// ============================================================================
// GLOBAL DEBUG FLAG
// ============================================================================

bool g_debug_enabled = false;

// ============================================================================
// MAIN CLI INTERFACE
// ============================================================================

typedef struct {
    bool verbose;
    bool debug;
    bool list_devices;
    bool bootstrap;
    bool read_firmware;
    bool write_firmware;
    int device_index;
    char *config_file;
    char *spl_file;
    char *uboot_file;
    char *output_file;
    char *input_file;
    bool no_erase;
    bool reboot_after;
    bool skip_ddr;
    uint32_t chunk_size; // Flash write chunk size (default: 131072)
    char *force_cpu;     // Force specific CPU variant (e.g., "a1", "t31x", "t31zx")
    char *firmware_dir;  // Firmware root directory (default: ./firmware)
    char *remote_host;   // Remote daemon host (NULL = local mode)
    int remote_port;     // Remote daemon port (default: 5050)
    char *auth_token;    // Auth token for remote daemon
    char *flash_chip;    // Override flash chip name (default: auto-detect)
} cli_options_t;

void print_usage(const char *program_name) {
    printf("Thingino Cloner - USB Device Cloner for Ingenic Processors\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -v, --verbose             Enable verbose logging\n");
    printf("  -d, --debug               Enable debug output\n");
    printf("  -l, --list                List connected devices\n");
    printf("  -i, --index <num>         Device index to operate on (default: 0)\n");
    printf("  -b, --bootstrap           Bootstrap device to firmware stage\n");
    printf("  -r, --read <file>         Read firmware from device to file\n");
    printf("  -w, --write <file>        Write firmware from file to device\n");
    printf("      --no-erase            Skip flash erase before writing\n");
    printf("      --reboot              Reboot device after flash write completes\n");
    printf("      --chunk-size <size>   Write chunk size: 64K, 128K, 256K, 512K, 1M\n");
    printf("      --cpu <variant>       CPU variant (a1, t31, t40, t41, etc.)\n");
    printf("      --config <file>       Custom DDR configuration file\n");
    printf("      --spl <file>          Custom SPL file\n");
    printf("      --uboot <file>        Custom U-Boot file\n");
    printf("      --firmware-dir <dir>  Firmware root directory (default: ./firmware)\n");
    printf("      --host <addr>         Connect to remote cloner-remote daemon\n");
    printf("      --port <port>         Remote daemon port (default: 5050)\n");
    printf("      --token <secret>      Auth token for remote daemon\n");
    printf("      --skip-ddr            Skip DDR configuration during bootstrap\n");
    printf("      --flash-chip <name>   Override flash chip (auto-detect from JEDEC ID)\n");
    printf("      --list-cpus           List supported CPU targets for --cpu\n");
    printf("\nExamples:\n");
    printf("  thingino-cloner -l                                 # List devices\n");
    printf("  thingino-cloner -i 0 -b --cpu t31                  # Bootstrap device 0 as T31\n");
    printf("  thingino-cloner -i 0 -b -w firmware.bin --cpu a1   # Bootstrap + write to A1\n");
    printf("  thingino-cloner --list-cpus                        # Show all supported CPUs\n");
}

static void print_supported_cpus(void) {
    printf("Supported CPU targets (use with --cpu <name>):\n\n");
    printf("  %-10s %-10s %s\n", "--cpu", "Arch", "Default DDR Chip");
    printf("  %-10s %-10s %s\n", "-----", "----", "----------------");
    size_t count;
    const processor_config_t *procs = processor_config_list(&count);
    for (size_t i = 0; i < count; i++) {
        const char *arch = procs[i].is_xburst2 ? "xburst2" : "xburst1";
        const ddr_chip_config_t *chip = ddr_chip_config_get_default(procs[i].name);
        printf("  %-10s %-10s %s\n", procs[i].name, arch, chip ? chip->name : "(none)");
    }
}

thingino_error_t parse_arguments(int argc, char *argv[], cli_options_t *options) {
    // Initialize options
    memset(options, 0, sizeof(cli_options_t));
    options->device_index = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            options->verbose = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            options->debug = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            options->list_devices = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bootstrap") == 0) {
            options->bootstrap = true;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--read") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->read_firmware = true;
            options->output_file = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--write") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->write_firmware = true;
            options->input_file = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->config_file = argv[++i];
        } else if (strcmp(argv[i], "--spl") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->spl_file = argv[++i];
        } else if (strcmp(argv[i], "--uboot") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->uboot_file = argv[++i];
        } else if (strcmp(argv[i], "--firmware-dir") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a directory path\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->firmware_dir = argv[++i];
        } else if (strcmp(argv[i], "--host") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a hostname\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->remote_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a port number\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->remote_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a token string\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->auth_token = argv[++i];
        } else if (strcmp(argv[i], "--flash-chip") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a chip name\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->flash_chip = argv[++i];
        } else if (strcmp(argv[i], "--list-cpus") == 0) {
            print_supported_cpus();
            exit(0);
        } else if (strcmp(argv[i], "--skip-ddr") == 0) {
            options->skip_ddr = true;
        } else if (strcmp(argv[i], "--no-erase") == 0) {
            options->no_erase = true;
        } else if (strcmp(argv[i], "--reboot") == 0) {
            options->reboot_after = true;
        } else if (strcmp(argv[i], "--chunk-size") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a size (e.g. 256K, 1M, 131072)\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            {
                char *endptr;
                unsigned long val = strtoul(argv[++i], &endptr, 10);
                if (endptr != argv[i] && (*endptr == 'k' || *endptr == 'K'))
                    { val *= 1024; endptr++; }
                else if (endptr != argv[i] && (*endptr == 'm' || *endptr == 'M'))
                    { val *= 1024 * 1024; endptr++; }
                if (*endptr == 'b' || *endptr == 'B')
                    endptr++;
                if (*endptr != '\0' || val == 0 || val > 0xFFFFFFFFUL) {
                    printf("Error: invalid chunk-size value\n");
                    return THINGINO_ERROR_INVALID_PARAMETER;
                }
                options->chunk_size = (uint32_t)val;
            }
        } else if (strcmp(argv[i], "--cpu") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a CPU variant (e.g., a1, t31x, t31zx)\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->force_cpu = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--index") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a device index\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->device_index = atoi(argv[++i]);
            if (options->device_index < 0) {
                printf("Error: device index must be >= 0\n");
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
        } else {
            printf("Error: Unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return THINGINO_ERROR_INVALID_PARAMETER;
        }
    }

    return THINGINO_SUCCESS;
}

thingino_error_t list_devices(usb_manager_t *manager) {
    printf("Scanning for Ingenic devices...\n\n");

    device_info_t *devices;
    int device_count;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (device_count == 0) {
        printf("No Ingenic devices found\n");
        return THINGINO_SUCCESS;
    }

    /* Auto-detect SoC for bootrom devices */
    for (int i = 0; i < device_count; i++) {
        if (devices[i].stage == STAGE_BOOTROM) {
            usb_device_t *dev = NULL;
            if (usb_manager_open_device(manager, &devices[i], &dev) == THINGINO_SUCCESS) {
                processor_variant_t detected = VARIANT_T31X;
                if (protocol_detect_soc(dev, &detected) == THINGINO_SUCCESS) {
                    devices[i].variant = detected;
                }
                usb_device_close(dev);
            }
        }
    }

    printf("Found %d device(s):\n", device_count);
    printf("Index | Bus | Addr | Vendor  | Product | Stage    | SoC\n");
    printf("------|-----|------|---------|---------|----------|--------\n");

    for (int i = 0; i < device_count; i++) {
        device_info_t *dev = &devices[i];
        const char *soc = dev->stage == STAGE_BOOTROM ? processor_variant_to_string(dev->variant) : "-";
        printf("  %3d | %3d | %4d | 0x%04X  | 0x%04X  | %-8s | %s\n", i, dev->bus, dev->address, dev->vendor,
               dev->product, device_stage_to_string(dev->stage), soc);
    }

    printf("\n");
    free(devices);
    return THINGINO_SUCCESS;
}

/* bootstrap_device_by_index, read_firmware_from_device, write_firmware_from_file
 * moved to libcloner/src/operations.c as cloner_op_bootstrap(),
 * cloner_op_read_firmware(), cloner_op_write_firmware(). */

int main(int argc, char *argv[]) {
    fprintf(stderr, "thingino-cloner %s (%s)\n", VERSION, GIT_HASH);

    /* Resolve default firmware dir relative to the binary location */
    static char default_fw_dir[4096];
#ifdef _WIN32
    {
        char exe_path[4096];
        GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        char *last_sep = strrchr(exe_path, '\\');
        if (last_sep)
            *last_sep = '\0';
        snprintf(default_fw_dir, sizeof(default_fw_dir), "%s\\firmware", exe_path);
    }
#else
    {
        char exe_path[4096];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            char *dir = dirname(exe_path);
            snprintf(default_fw_dir, sizeof(default_fw_dir), "%s/firmware", dir);
        } else {
            snprintf(default_fw_dir, sizeof(default_fw_dir), "./firmware");
        }
    }
#endif

    cli_options_t options;
    thingino_error_t result = parse_arguments(argc, argv, &options);
    if (result != THINGINO_SUCCESS) {
        return 1;
    }

    /* Use binary-relative firmware dir if user didn't specify one */
    if (!options.firmware_dir)
        options.firmware_dir = default_fw_dir;

    // Set global debug flag based on CLI options
    g_debug_enabled = options.debug;

    // Remote mode: dispatch to cloner-remote daemon
    if (options.remote_host) {
        int port = options.remote_port > 0 ? options.remote_port : CLONER_DEFAULT_PORT;
        if (remote_connect(options.remote_host, port, options.auth_token) < 0)
            return EXIT_PROTOCOL_ERROR;

        int rc = 0;
        const char *cpu = options.force_cpu;
        if (!cpu) {
            cpu = remote_detect_variant(options.device_index);
            if (cpu) {
                printf("Auto-detected remote device: %s\n", cpu);
            } else {
                fprintf(stderr, "Failed to detect remote device variant\n");
                remote_disconnect();
                return EXIT_DEVICE_ERROR;
            }
        }

        if (options.list_devices) {
            rc = remote_list_devices() < 0 ? EXIT_DEVICE_ERROR : 0;
        } else if (options.write_firmware && options.input_file && options.bootstrap) {
            /* Combined bootstrap + write (remote) */
            rc = remote_bootstrap(options.device_index, cpu, options.firmware_dir);
            if (rc == 0) {
                printf("Bootstrap complete, proceeding to write\n\n");
                rc = remote_write_firmware(options.device_index, cpu, options.input_file);
            }
            rc = rc < 0 ? EXIT_TRANSFER_ERROR : 0;
        } else if (options.bootstrap) {
            rc = remote_bootstrap(options.device_index, cpu, options.firmware_dir) < 0 ? EXIT_DEVICE_ERROR : 0;
        } else if (options.write_firmware && options.input_file) {
            rc = remote_write_firmware(options.device_index, cpu, options.input_file) < 0 ? EXIT_TRANSFER_ERROR : 0;
        } else if (options.read_firmware && options.output_file) {
            rc = remote_read_firmware(options.device_index, options.output_file) < 0 ? EXIT_TRANSFER_ERROR : 0;
        } else {
            printf("Remote mode: specify -l, -b, -w, or -r\n");
            rc = 1;
        }

        remote_disconnect();
        return rc < 0 ? 1 : 0;
    }

    // Local mode
    if (!options.list_devices && !options.bootstrap && !options.read_firmware && !options.write_firmware) {
        printf("No action specified. Use -h for help.\n");
        return 1;
    }

    usb_manager_t manager;
    result = usb_manager_init(&manager);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to initialize USB: %s\n", thingino_error_to_string(result));
        return EXIT_DEVICE_ERROR;
    }

    int exit_code = 0;

    /* List devices and exit */
    if (options.list_devices) {
        result = list_devices(&manager);
        usb_manager_cleanup(&manager);
        return result != THINGINO_SUCCESS ? EXIT_DEVICE_ERROR : 0;
    }

    /* Bootstrap (required for all device operations from bootrom) */
    const char *detected_cpu = options.force_cpu;
    if (options.bootstrap) {
        result =
            cloner_op_bootstrap(&manager, options.device_index, options.force_cpu, options.verbose, options.skip_ddr,
                                options.config_file, options.spl_file, options.uboot_file, options.firmware_dir);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Bootstrap failed: %s\n", thingino_error_to_string(result));
            usb_manager_cleanup(&manager);
            return EXIT_DEVICE_ERROR;
        }
        /* If no --cpu was given, bootstrap auto-detected the variant.
         * Retrieve it so we can pass it to write/read operations, which
         * otherwise can't auto-detect (device is now in firmware stage). */
        if (!detected_cpu) {
            detected_cpu = cloner_get_last_detected_variant();
        }
    }

    /* Write firmware */
    if (options.write_firmware && options.input_file) {
        result = cloner_op_write_firmware(
            &manager, options.device_index, options.input_file, detected_cpu, options.flash_chip, options.no_erase,
            options.reboot_after, options.bootstrap, options.verbose, options.skip_ddr, options.config_file,
            options.spl_file, options.uboot_file, options.firmware_dir, options.chunk_size);
        if (result != THINGINO_SUCCESS)
            exit_code = EXIT_TRANSFER_ERROR;
    }

    /* Read firmware */
    if (options.read_firmware && options.output_file) {
        result = cloner_op_read_firmware(&manager, options.device_index, options.output_file, detected_cpu,
                                         options.flash_chip);
        if (result != THINGINO_SUCCESS)
            exit_code = EXIT_TRANSFER_ERROR;
    }

    usb_manager_cleanup(&manager);
    return exit_code;
}
