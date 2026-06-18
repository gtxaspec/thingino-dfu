#include "tdfu/tdfu.h"
#include "tdfu/core.h"
#include "tdfu/dfu.h"
#include "tdfu/protocol.h"
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
    bool cloner;         // Use the legacy cloner backend (default backend is DFU)
    char *alt;           // DFU alt-setting: name or number
    bool wait;           // Wait for the required device to appear before proceeding
} cli_options_t;

void print_usage(const char *program_name) {
    printf("thingino-dfu - USB flashing tool for Ingenic SoCs\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -v, --verbose             Enable verbose logging\n");
    printf("  -d, --debug               Enable debug output\n");
    printf("  -l, --list                List connected devices\n");
    printf("  -i, --index <num>         Device index to operate on (default: 0)\n");
    printf("  -b, --bootstrap           Bootstrap device to firmware stage\n");
    printf("  -r, --read <file>         Read firmware from device to file\n");
    printf("  -w, --write <file>        Bootstrap (if needed) and write firmware to device\n");
    printf("      --no-erase            Skip flash erase before writing\n");
    printf("      --reboot              Reboot after write (cloner backend only)\n");
    printf("      --chunk-size <size>   Write chunk size: 64K, 128K, 256K, 512K, 1M\n");
    printf("      --cpu <variant>       CPU variant (a1, t31, t40, t41, etc.)\n");
    printf("      --config <file>       Custom DDR configuration file\n");
    printf("      --spl <file>          Custom SPL file\n");
    printf("      --uboot <file>        Custom U-Boot file\n");
    printf("      --firmware-dir <dir>  Firmware root directory (default: ./firmware)\n");
    printf("      --host <addr>         Connect to remote dfu-remote daemon\n");
    printf("      --port <port>         Remote daemon port (default: 5050)\n");
    printf("      --token <secret>      Auth token for remote daemon\n");
    printf("      --skip-ddr            Skip DDR configuration during bootstrap\n");
    printf("      --flash-chip <name>   Override flash chip (auto-detect from JEDEC ID)\n");
    printf("      --list-cpus           List supported CPU targets for --cpu\n");
    printf("      --cloner              Use the legacy cloner backend (default: DFU)\n");
    printf("      --alt <name|num>      DFU alt-setting to target\n");
    printf("      --wait                Wait for the required device to appear, then proceed\n");
    printf("\nExamples (DFU is the default backend):\n");
    printf("  thingino-dfu -b                                 # Bootrom -> U-Boot DFU (auto-detect SoC)\n");
    printf("  thingino-dfu --spl spl.bin --uboot u-boot.bin   # USB-boot custom blobs (bootstrap implied)\n");
    printf("  thingino-dfu -l                                 # List DFU alt-settings\n");
    printf("  thingino-dfu --alt rootfs -w rootfs.bin         # Flash an alt-setting via DFU\n");
    printf("  thingino-dfu --host 192.168.1.50 -w fw.bin      # Remote: bootstrap (if needed) + write\n");
    printf("  thingino-dfu --cloner -w fw.bin                 # Legacy cloner: bootstrap + write\n");
    printf("  thingino-dfu --list-cpus                        # Show all supported CPUs\n");
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

tdfu_error_t parse_arguments(int argc, char *argv[], cli_options_t *options) {
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
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->read_firmware = true;
            options->output_file = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--write") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->write_firmware = true;
            options->input_file = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->config_file = argv[++i];
        } else if (strcmp(argv[i], "--spl") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->spl_file = argv[++i];
        } else if (strcmp(argv[i], "--uboot") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->uboot_file = argv[++i];
        } else if (strcmp(argv[i], "--firmware-dir") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a directory path\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->firmware_dir = argv[++i];
        } else if (strcmp(argv[i], "--cloner") == 0) {
            options->cloner = true;
        } else if (strcmp(argv[i], "--wait") == 0) {
            options->wait = true;
        } else if (strcmp(argv[i], "--alt") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires an alt setting (name or number)\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->alt = argv[++i];
        } else if (strcmp(argv[i], "--host") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a hostname\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->remote_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a port number\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->remote_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a token string\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->auth_token = argv[++i];
        } else if (strcmp(argv[i], "--flash-chip") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a chip name\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
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
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            {
                char *endptr;
                unsigned long val = strtoul(argv[++i], &endptr, 10);
                if (endptr != argv[i] && (*endptr == 'k' || *endptr == 'K')) {
                    val *= 1024;
                    endptr++;
                } else if (endptr != argv[i] && (*endptr == 'm' || *endptr == 'M')) {
                    val *= 1024 * 1024;
                    endptr++;
                }
                if (*endptr == 'b' || *endptr == 'B')
                    endptr++;
                if (*endptr != '\0' || val == 0 || val > 0xFFFFFFFFUL) {
                    printf("Error: invalid chunk-size value\n");
                    return TDFU_ERROR_INVALID_PARAMETER;
                }
                options->chunk_size = (uint32_t)val;
            }
        } else if (strcmp(argv[i], "--cpu") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a CPU variant (e.g., a1, t31x, t31zx)\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->force_cpu = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--index") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a device index\n", argv[i]);
                return TDFU_ERROR_INVALID_PARAMETER;
            }
            options->device_index = atoi(argv[++i]);
            if (options->device_index < 0) {
                printf("Error: device index must be >= 0\n");
                return TDFU_ERROR_INVALID_PARAMETER;
            }
        } else {
            printf("Error: Unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return TDFU_ERROR_INVALID_PARAMETER;
        }
    }

    return TDFU_SUCCESS;
}

tdfu_error_t list_devices(usb_manager_t *manager) {
    printf("Scanning for Ingenic devices...\n\n");

    tdfu_device_info_t *devices;
    int device_count;
    tdfu_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != TDFU_SUCCESS) {
        printf("Failed to list devices: %s\n", tdfu_error_to_string(result));
        return result;
    }

    if (device_count == 0) {
        printf("No Ingenic devices found\n");
        return TDFU_SUCCESS;
    }

    /* Auto-detect SoC for bootrom devices */
    for (int i = 0; i < device_count; i++) {
        if (devices[i].stage == TDFU_STAGE_BOOTROM) {
            usb_device_t *dev = NULL;
            if (usb_manager_open_device(manager, &devices[i], &dev) == TDFU_SUCCESS) {
                tdfu_variant_t detected = TDFU_VARIANT_T31X;
                if (protocol_detect_soc(dev, &detected) == TDFU_SUCCESS) {
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
        tdfu_device_info_t *dev = &devices[i];
        const char *soc = dev->stage == TDFU_STAGE_BOOTROM ? tdfu_variant_to_string(dev->variant) : "-";
        printf("  %3d | %3d | %4d | 0x%04X  | 0x%04X  | %-8s | %s\n", i, dev->bus, dev->address, dev->vendor,
               dev->product, tdfu_stage_to_string(dev->stage), soc);
    }

    printf("\n");
    free(devices);
    return TDFU_SUCCESS;
}

/* bootstrap_device_by_index, read_firmware_from_device, write_firmware_from_file
 * moved to libtdfu/src/operations.c as tdfu_op_bootstrap(),
 * tdfu_op_read_firmware(), tdfu_op_write_firmware(). */

/* Block until the device the operation needs appears (for --wait). want_gadget
 * waits for the U-Boot DFU gadget (a108:4d44); otherwise it waits for a bootrom/
 * firmware device. Polls every 500ms, indefinitely (Ctrl-C to abort). */
static void wait_for_device(usb_manager_t *manager, bool want_gadget) {
    bool announced = false;
    for (;;) {
        bool present;
        if (want_gadget) {
            present = tdfu_dfu_gadget_present(manager);
        } else {
            tdfu_device_info_t *devs = NULL;
            int n = 0;
            present = (usb_manager_find_devices(manager, &devs, &n) == TDFU_SUCCESS && n > 0);
            free(devs);
        }
        if (present) {
            if (announced)
                LOG_INFO("Device found.\n");
            return;
        }
        if (!announced) {
            LOG_INFO("Waiting for %s to appear (Ctrl-C to abort)...\n",
                     want_gadget ? "U-Boot DFU gadget" : "device");
            announced = true;
        }
        tdfu_sleep_milliseconds(500);
    }
}

int main(int argc, char *argv[]) {
    fprintf(stderr, "thingino-dfu %s (%s)\n", VERSION, GIT_HASH);

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
    tdfu_error_t result = parse_arguments(argc, argv, &options);
    if (result != TDFU_SUCCESS) {
        return 1;
    }

    /* Use binary-relative firmware dir if user didn't specify one */
    if (!options.firmware_dir)
        options.firmware_dir = default_fw_dir;

    /* A custom --spl + --uboot pair with no action implies bootstrap
     * (t31-usbboot.py ergonomics): USB-booting them is the only thing a
     * pair of boot blobs on its own can mean. Applies to local and remote. */
    if (!options.list_devices && !options.bootstrap && !options.read_firmware && !options.write_firmware &&
        options.spl_file && options.uboot_file) {
        options.bootstrap = true;
    }

    // Set global debug flag based on CLI options
    g_debug_enabled = options.debug;

    // Remote mode: dispatch to dfu-remote daemon (DFU by default, --cloner for legacy)
    if (options.remote_host) {
        int port = options.remote_port > 0 ? options.remote_port : TDFU_DEFAULT_PORT;
        if (remote_connect(options.remote_host, port, options.auth_token) < 0)
            return EXIT_PROTOCOL_ERROR;
        remote_set_cloner(options.cloner);

        int rc = 0;
        /* Variant detection probes a bootrom; it's needed for cloner operations
         * and for DFU bootstrap, but NOT for a DFU read/write (those target an
         * already-running U-Boot DFU gadget, which has no SoC variant). */
        const char *cpu = options.force_cpu;
        /* Custom DFU SPL+U-Boot (both --spl and --uboot) override the daemon's
         * firmware/dfu/<soc>/ and skip SoC detection, so no variant is needed. */
        bool dfu_custom_blobs = !options.cloner && options.spl_file && options.spl_file[0] &&
                                options.uboot_file && options.uboot_file[0];
        bool need_variant = (options.cloner || options.bootstrap) && !dfu_custom_blobs;
        if (need_variant && !cpu) {
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
        } else if (options.write_firmware && options.input_file) {
            /* -w bootstraps then writes, mirroring local cloner mode: bootstrap
             * if the user passed -b or the target is still in bootrom; if it has
             * already re-enumerated as a DFU/firmware gadget, write straight away
             * (re-bootstrapping a gadget would fail). */
            int stage = remote_device_stage(options.device_index); /* 0=bootrom 1=fw <0=unknown */
            bool do_bootstrap = options.bootstrap || stage == 0;
            rc = 0;
            if (do_bootstrap) {
                if (!cpu && !dfu_custom_blobs)
                    cpu = remote_detect_variant(options.device_index);
                if (!cpu && !dfu_custom_blobs) {
                    fprintf(stderr, "Failed to detect remote device variant for bootstrap\n");
                    rc = -1;
                } else {
                    rc = remote_bootstrap(options.device_index, cpu, options.firmware_dir, options.spl_file,
                                          options.uboot_file);
                    if (rc == 0)
                        printf("Bootstrap complete, proceeding to write\n\n");
                }
            }
            if (rc == 0)
                rc = remote_write_firmware(options.device_index, cpu, options.input_file);
            rc = rc < 0 ? EXIT_TRANSFER_ERROR : 0;
        } else if (options.bootstrap) {
            rc = remote_bootstrap(options.device_index, cpu, options.firmware_dir, options.spl_file,
                                  options.uboot_file) < 0
                     ? EXIT_DEVICE_ERROR
                     : 0;
        } else if (options.read_firmware && options.output_file) {
            rc = remote_read_firmware(options.device_index, options.output_file) < 0 ? EXIT_TRANSFER_ERROR : 0;
        } else {
            printf("Remote mode: specify -l, -b, -w, or -r\n");
            rc = 1;
        }

        remote_disconnect();
        return rc; /* 0 or EXIT_* - every branch above normalizes negatives */
    }

    // Local mode
    if (!options.list_devices && !options.bootstrap && !options.read_firmware && !options.write_firmware) {
        printf("No action specified. Use -h for help.\n");
        return 1;
    }

    usb_manager_t manager;
    result = usb_manager_init(&manager);
    if (result != TDFU_SUCCESS) {
        printf("Failed to initialize USB: %s\n", tdfu_error_to_string(result));
        return EXIT_DEVICE_ERROR;
    }

    int exit_code = 0;

    /* --wait: block until the needed device shows up. DFU bootstrap and cloner
     * need a bootrom/firmware device; DFU read/write/list needs the gadget. */
    if (options.wait)
        wait_for_device(&manager, !options.cloner && !options.bootstrap);

    /* DFU mode: device is already running U-Boot's `dfu` command */
    /* DFU is the default backend; --cloner selects the legacy cloner path. */
    if (!options.cloner) {
        /* -b: bootstrap a bootrom device into U-Boot DFU mode */
        if (options.bootstrap) {
            result = tdfu_dfu_bootstrap(&manager, options.device_index, options.firmware_dir, options.force_cpu,
                                        options.spl_file, options.uboot_file);
            usb_manager_cleanup(&manager);
            if (result != TDFU_SUCCESS) {
                LOG_ERROR("DFU bootstrap failed: %s\n", tdfu_error_to_string(result));
                return EXIT_DEVICE_ERROR;
            }
            printf("Re-run with -l / -w / -r once the DFU device enumerates.\n");
            return 0;
        }
        /* -l: list every connected Ingenic device (bootrom AND DFU), like the
         * cloner/remote paths. Probing only for a DFU gadget can't see a bootrom
         * device - and the probe's recovery path would issue a needless USB reset
         * on it. So print the inventory, then show DFU alt-settings only when the
         * targeted device is actually a gadget. */
        if (options.list_devices) {
            result = list_devices(&manager);
            tdfu_device_info_t *devs = NULL;
            int n = 0;
            if (usb_manager_find_devices(&manager, &devs, &n) == TDFU_SUCCESS) {
                if (options.device_index >= 0 && options.device_index < n &&
                    devs[options.device_index].stage == TDFU_STAGE_DFU) {
                    tdfu_dfu_info_t dfu_info;
                    if (tdfu_dfu_probe(&manager, options.device_index, &dfu_info) == TDFU_SUCCESS) {
                        printf("\nDFU device %d: %d alt setting(s), transfer size %u bytes, DFU %x.%02x\n",
                               options.device_index, dfu_info.alt_count, dfu_info.transfer_size,
                               (dfu_info.bcd_dfu >> 8) & 0xff, dfu_info.bcd_dfu & 0xff);
                        for (int i = 0; i < dfu_info.alt_count; i++)
                            printf("  alt %d: \"%s\"\n", dfu_info.alts[i].alt, dfu_info.alts[i].name);
                    }
                }
                free(devs);
            }
            usb_manager_cleanup(&manager);
            return result != TDFU_SUCCESS ? EXIT_DEVICE_ERROR : 0;
        }

        tdfu_dfu_info_t dfu_info;
        result = tdfu_dfu_probe(&manager, options.device_index, &dfu_info);
        if (result != TDFU_SUCCESS) {
            LOG_ERROR("DFU probe failed: %s\n", tdfu_error_to_string(result));
            usb_manager_cleanup(&manager);
            return EXIT_DEVICE_ERROR;
        }
        int alt = -1;
        if (options.alt)
            alt = tdfu_dfu_find_alt(&dfu_info, options.alt);
        else if (dfu_info.alt_count == 1)
            alt = dfu_info.alts[0].alt;
        if (alt < 0) {
            LOG_ERROR("Specify --alt <name|num> (use -l to list alt settings)\n");
            usb_manager_cleanup(&manager);
            return EXIT_DEVICE_ERROR;
        }
        if (options.write_firmware && options.input_file)
            result = tdfu_dfu_download(&manager, options.device_index, alt, options.input_file);
        else if (options.read_firmware && options.output_file)
            result = tdfu_dfu_upload(&manager, options.device_index, alt, options.output_file, 0);
        else {
            LOG_ERROR("DFU mode needs -l (list alts), -w <file> (download), or -r <file> (upload)\n");
            result = TDFU_ERROR_INVALID_PARAMETER;
        }
        usb_manager_cleanup(&manager);
        return result != TDFU_SUCCESS ? EXIT_TRANSFER_ERROR : 0;
    }

    /* List devices and exit */
    if (options.list_devices) {
        result = list_devices(&manager);
        usb_manager_cleanup(&manager);
        return result != TDFU_SUCCESS ? EXIT_DEVICE_ERROR : 0;
    }

    /* Bootstrap (required for all device operations from bootrom) */
    const char *detected_cpu = options.force_cpu;
    if (options.bootstrap) {
        result = tdfu_op_bootstrap(&manager, options.device_index, options.force_cpu, options.verbose, options.skip_ddr,
                                   options.config_file, options.spl_file, options.uboot_file, options.firmware_dir);
        if (result != TDFU_SUCCESS) {
            LOG_ERROR("Bootstrap failed: %s\n", tdfu_error_to_string(result));
            usb_manager_cleanup(&manager);
            return EXIT_DEVICE_ERROR;
        }
        /* If no --cpu was given, bootstrap auto-detected the variant.
         * Retrieve it so we can pass it to write/read operations, which
         * otherwise can't auto-detect (device is now in firmware stage). */
        if (!detected_cpu) {
            detected_cpu = tdfu_get_last_detected_variant();
        }
    }

    /* Write firmware */
    if (options.write_firmware && options.input_file) {
        result = tdfu_op_write_firmware(&manager, options.device_index, options.input_file, detected_cpu,
                                        options.flash_chip, options.no_erase, options.reboot_after, options.bootstrap,
                                        options.verbose, options.skip_ddr, options.config_file, options.spl_file,
                                        options.uboot_file, options.firmware_dir, options.chunk_size);
        if (result != TDFU_SUCCESS)
            exit_code = EXIT_TRANSFER_ERROR;
    }

    /* Read firmware */
    if (options.read_firmware && options.output_file) {
        result = tdfu_op_read_firmware(&manager, options.device_index, options.output_file, detected_cpu,
                                       options.flash_chip);
        if (result != TDFU_SUCCESS)
            exit_code = EXIT_TRANSFER_ERROR;
    }

    usb_manager_cleanup(&manager);
    return exit_code;
}
