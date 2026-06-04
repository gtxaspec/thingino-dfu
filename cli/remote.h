#ifndef TDFU_REMOTE_H
#define TDFU_REMOTE_H

int remote_connect(const char *host, int port, const char *token);
void remote_disconnect(void);
int remote_list_devices(void);
const char *remote_detect_variant(int device_index);
int remote_bootstrap(int device_index, const char *cpu_variant, const char *firmware_dir);
int remote_write_firmware(int device_index, const char *cpu_variant, const char *firmware_file);
int remote_read_firmware(int device_index, const char *output_file);

#endif
