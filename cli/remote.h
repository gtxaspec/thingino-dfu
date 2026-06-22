#ifndef TDFU_REMOTE_H
#define TDFU_REMOTE_H

#include <stdbool.h>

/* Select the daemon backend: false = DFU (default), true = legacy cloner. */
void remote_set_cloner(bool on);

int remote_connect(const char *host, int port, const char *token);
void remote_disconnect(void);
int remote_list_devices(void);
const char *remote_detect_variant(int device_index);
int remote_device_stage(int device_index);
int remote_bootstrap(int device_index, const char *cpu_variant, const char *firmware_dir, const char *spl_file,
                     const char *uboot_file);
int remote_write_firmware(int device_index, const char *cpu_variant, const char *firmware_file, const char *alt);
int remote_read_firmware(int device_index, const char *output_file, const char *alt);

#endif
