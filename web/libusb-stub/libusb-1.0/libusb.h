/**
 * Minimal libusb-1.0 stub header for Emscripten/WebUSB builds.
 *
 * Types and constants match the real libusb API. Functions are
 * implemented in libusb-webusb.js via Emscripten's --js-library.
 */

#ifndef LIBUSB_STUB_H
#define LIBUSB_STUB_H

#include <stdint.h>
#include <sys/types.h>

/* Opaque types — the JS shim manages actual state */
typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

/* Device descriptor */
struct libusb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
};

/* Error codes */
enum libusb_error {
    LIBUSB_SUCCESS             =  0,
    LIBUSB_ERROR_IO            = -1,
    LIBUSB_ERROR_INVALID_PARAM = -2,
    LIBUSB_ERROR_ACCESS        = -3,
    LIBUSB_ERROR_NO_DEVICE     = -4,
    LIBUSB_ERROR_NOT_FOUND     = -5,
    LIBUSB_ERROR_BUSY          = -6,
    LIBUSB_ERROR_TIMEOUT       = -7,
    LIBUSB_ERROR_OVERFLOW      = -8,
    LIBUSB_ERROR_PIPE          = -9,
    LIBUSB_ERROR_INTERRUPTED   = -10,
    LIBUSB_ERROR_NO_MEM        = -11,
    LIBUSB_ERROR_NOT_SUPPORTED = -12,
    LIBUSB_ERROR_OTHER         = -99,
};

/* Function prototypes — implemented by JS shim */
int  libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list);
void    libusb_free_device_list(libusb_device **list, int unref_devices);

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc);
uint8_t libusb_get_bus_number(libusb_device *dev);
uint8_t libusb_get_device_address(libusb_device *dev);
int libusb_get_port_numbers(libusb_device *dev, uint8_t *port_numbers, int port_numbers_len);

int  libusb_open(libusb_device *dev, libusb_device_handle **handle);
void libusb_close(libusb_device_handle *handle);

libusb_device *libusb_ref_device(libusb_device *dev);
void libusb_unref_device(libusb_device *dev);

int libusb_set_configuration(libusb_device_handle *handle, int configuration);
int libusb_get_configuration(libusb_device_handle *handle, int *config);
int libusb_claim_interface(libusb_device_handle *handle, int interface_number);
int libusb_release_interface(libusb_device_handle *handle, int interface_number);

int libusb_kernel_driver_active(libusb_device_handle *handle, int interface_number);
int libusb_detach_kernel_driver(libusb_device_handle *handle, int interface_number);

int libusb_control_transfer(libusb_device_handle *handle, uint8_t bmRequestType,
                            uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                            unsigned char *data, uint16_t wLength, unsigned int timeout);

int libusb_bulk_transfer(libusb_device_handle *handle, unsigned char endpoint,
                         unsigned char *data, int length, int *transferred,
                         unsigned int timeout);

int libusb_interrupt_transfer(libusb_device_handle *handle, unsigned char endpoint,
                              unsigned char *data, int length, int *transferred,
                              unsigned int timeout);

int libusb_reset_device(libusb_device_handle *handle);

const char *libusb_error_name(int errcode);

#endif /* LIBUSB_STUB_H */
