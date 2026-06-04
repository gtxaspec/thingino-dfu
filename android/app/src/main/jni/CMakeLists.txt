cmake_minimum_required(VERSION 3.22.1)
project(tdfu_jni C)

# Path to libcloner sources (relative to this CMakeLists.txt)
# CMAKE_CURRENT_SOURCE_DIR = android/app/src/main/jni/
# libcloner is at the repo root: thingino-dfu/libcloner/
# jni/ -> main/ -> src/ -> app/ -> android/ -> thingino-dfu/
set(LIBCLONER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../libcloner")

# ---------------------------------------------------------------------------
# libusb -- fetch source for Android cross-compile
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG        v1.0.27
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libusb)

set(LIBUSB_SRC_DIR "${libusb_SOURCE_DIR}/libusb")

# Create a directory layout so #include <libusb-1.0/libusb.h> works.
# libcloner uses this include path; libusb source tree has libusb/libusb.h.
set(LIBUSB_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/libusb-compat")
file(MAKE_DIRECTORY "${LIBUSB_COMPAT_DIR}/libusb-1.0")
# Copy the header (done at configure time)
file(COPY "${LIBUSB_SRC_DIR}/libusb.h" DESTINATION "${LIBUSB_COMPAT_DIR}/libusb-1.0/")

# Generate config.h for libusb Android build
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.h" "\
#ifndef LIBUSB_CONFIG_H
#define LIBUSB_CONFIG_H

#define DEFAULT_VISIBILITY __attribute__((visibility(\"default\")))
#define ENABLE_LOGGING 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_DECL_EFD_CLOEXEC 1
#define HAVE_DECL_EFD_NONBLOCK 1
#define HAVE_DECL_TFD_CLOEXEC 1
#define HAVE_DECL_TFD_NONBLOCK 1
#define HAVE_DLFCN_H 1
#define HAVE_EVENTFD 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_NFDS_T 1
#define HAVE_PIPE2 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRUCT_TIMESPEC 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIMERFD 1
#define HAVE_UNISTD_H 1
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))
#define HAVE_ASM_TYPES_H 1

#define PLATFORM_POSIX 1

#endif
")

# Build libusb as a static library
add_library(usb1.0 STATIC
    "${LIBUSB_SRC_DIR}/core.c"
    "${LIBUSB_SRC_DIR}/descriptor.c"
    "${LIBUSB_SRC_DIR}/hotplug.c"
    "${LIBUSB_SRC_DIR}/io.c"
    "${LIBUSB_SRC_DIR}/sync.c"
    "${LIBUSB_SRC_DIR}/strerror.c"
    "${LIBUSB_SRC_DIR}/os/linux_usbfs.c"
    "${LIBUSB_SRC_DIR}/os/linux_netlink.c"
    "${LIBUSB_SRC_DIR}/os/events_posix.c"
    "${LIBUSB_SRC_DIR}/os/threads_posix.c"
)

target_include_directories(usb1.0 PUBLIC
    "${LIBUSB_SRC_DIR}"
    "${LIBUSB_COMPAT_DIR}"
)

target_include_directories(usb1.0 PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_definitions(usb1.0 PRIVATE
    HAVE_CONFIG_H
    _GNU_SOURCE
    OS_LINUX
    PLATFORM_POSIX
)

target_link_libraries(usb1.0 log)

# ---------------------------------------------------------------------------
# libcloner sources (compiled directly into the JNI shared library)
# ---------------------------------------------------------------------------
set(LIBCLONER_SOURCES
    "${LIBCLONER_DIR}/src/core.c"
    "${LIBCLONER_DIR}/src/usb/manager.c"
    "${LIBCLONER_DIR}/src/usb/device.c"
    "${LIBCLONER_DIR}/src/usb/protocol.c"
    "${LIBCLONER_DIR}/src/firmware/loader.c"
    "${LIBCLONER_DIR}/src/firmware/reader.c"
    "${LIBCLONER_DIR}/src/firmware/writer.c"
    "${LIBCLONER_DIR}/src/firmware/handshake.c"
    "${LIBCLONER_DIR}/src/firmware/flash_descriptor.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_utils.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_controller.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_param_builder.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_phy.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_phy_ddr2.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_ctrl_txx.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_generator.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_binary_builder.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_rdd_ddr2.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_rdd_ddr3.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_fidb_xb1.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_fidb_xb2.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_rdd_xb2.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_config_database.c"
    "${LIBCLONER_DIR}/src/ddr/ddr_reg_compute.c"
    "${LIBCLONER_DIR}/src/flash/spi_nor_db.c"
    "${LIBCLONER_DIR}/src/flash/nand_db.c"
    "${LIBCLONER_DIR}/src/platforms/platform.c"
    "${LIBCLONER_DIR}/src/platforms/t10.c"
    "${LIBCLONER_DIR}/src/platforms/t20.c"
    "${LIBCLONER_DIR}/src/platforms/t21.c"
    "${LIBCLONER_DIR}/src/platforms/t30.c"
    "${LIBCLONER_DIR}/src/platforms/t31.c"
    "${LIBCLONER_DIR}/src/platforms/t31a.c"
    "${LIBCLONER_DIR}/src/platforms/t40.c"
    "${LIBCLONER_DIR}/src/platforms/t41.c"
    "${LIBCLONER_DIR}/src/platforms/a1.c"
    "${LIBCLONER_DIR}/src/platform.c"
    "${LIBCLONER_DIR}/src/bootstrap.c"
    "${LIBCLONER_DIR}/src/utils.c"
    "${LIBCLONER_DIR}/src/operations.c"
)

# ---------------------------------------------------------------------------
# JNI shared library: tdfu_jni
# Contains the JNI bridge code + all libcloner sources compiled in.
# Statically links libusb.
# ---------------------------------------------------------------------------
add_library(tdfu_jni SHARED
    tdfu_jni.c
    ${LIBCLONER_SOURCES}
)

# Include paths:
# - LIBUSB_COMPAT_DIR provides <libusb-1.0/libusb.h>
# - LIBUSB_SRC_DIR provides "libusbi.h" (internal, needed by libusb itself)
# - LIBCLONER_DIR/include provides <cloner/...> and <thingino.h>
# - LIBCLONER_DIR/src/* provide internal headers (ddr, firmware, flash, platform)
target_include_directories(tdfu_jni PRIVATE
    "${LIBUSB_COMPAT_DIR}"
    "${LIBUSB_SRC_DIR}"
    "${LIBCLONER_DIR}/include"
    "${LIBCLONER_DIR}/src"
    "${LIBCLONER_DIR}/src/ddr"
    "${LIBCLONER_DIR}/src/firmware"
    "${LIBCLONER_DIR}/src/flash"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tdfu_jni
    usb1.0
    log
    android
)

target_compile_definitions(tdfu_jni PRIVATE
    ANDROID
    _GNU_SOURCE
)

# Reduce binary size: strip unused symbols
set_target_properties(tdfu_jni PROPERTIES
    C_VISIBILITY_PRESET hidden
)

# 16KB page size alignment (required for Android 15+ / API 35)
target_link_options(tdfu_jni PRIVATE "LINKER:-z,max-page-size=16384")
