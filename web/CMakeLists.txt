cmake_minimum_required(VERSION 3.10)
project(thingino-dfu-web LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Version
# Version from git tag
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_TAG)
    string(REGEX REPLACE "^v" "" WEB_VERSION "${GIT_TAG}")
else()
    set(WEB_VERSION "dev")
endif()
add_definitions(-DVERSION="${WEB_VERSION}" -DGIT_HASH="wasm")

# Global debug flag (defined in our web glue)
# The library references g_debug_enabled; we define it in web_main.c

# Use our stub libusb header (functions implemented by JS shim)
set(LIBUSB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/libusb-stub")

# Pull in all libtdfu sources
set(LIBTDFU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libtdfu")

set(LIBTDFU_SOURCES
    ${LIBTDFU_DIR}/src/core.c
    ${LIBTDFU_DIR}/src/usb/manager.c
    ${LIBTDFU_DIR}/src/usb/device.c
    ${LIBTDFU_DIR}/src/usb/protocol.c
    ${LIBTDFU_DIR}/src/firmware/loader.c
    ${LIBTDFU_DIR}/src/firmware/reader.c
    ${LIBTDFU_DIR}/src/firmware/writer.c
    ${LIBTDFU_DIR}/src/firmware/handshake.c
    ${LIBTDFU_DIR}/src/firmware/flash_descriptor.c
    ${LIBTDFU_DIR}/src/ddr/ddr_utils.c
    ${LIBTDFU_DIR}/src/ddr/ddr_controller.c
    ${LIBTDFU_DIR}/src/ddr/ddr_param_builder.c
    ${LIBTDFU_DIR}/src/ddr/ddr_phy.c
    ${LIBTDFU_DIR}/src/ddr/ddr_phy_ddr2.c
    ${LIBTDFU_DIR}/src/ddr/ddr_ctrl_txx.c
    ${LIBTDFU_DIR}/src/ddr/ddr_generator.c
    ${LIBTDFU_DIR}/src/ddr/ddr_binary_builder.c
    ${LIBTDFU_DIR}/src/ddr/ddr_rdd_ddr2.c
    ${LIBTDFU_DIR}/src/ddr/ddr_rdd_ddr3.c
    ${LIBTDFU_DIR}/src/ddr/ddr_fidb_xb1.c
    ${LIBTDFU_DIR}/src/ddr/ddr_fidb_xb2.c
    ${LIBTDFU_DIR}/src/ddr/ddr_rdd_xb2.c
    ${LIBTDFU_DIR}/src/ddr/ddr_config_database.c
    ${LIBTDFU_DIR}/src/ddr/ddr_reg_compute.c
    ${LIBTDFU_DIR}/src/flash/spi_nor_db.c
    ${LIBTDFU_DIR}/src/flash/nand_db.c
    ${LIBTDFU_DIR}/src/platforms/platform.c
    ${LIBTDFU_DIR}/src/platforms/t10.c
    ${LIBTDFU_DIR}/src/platforms/t20.c
    ${LIBTDFU_DIR}/src/platforms/t21.c
    ${LIBTDFU_DIR}/src/platforms/t30.c
    ${LIBTDFU_DIR}/src/platforms/t31.c
    ${LIBTDFU_DIR}/src/platforms/t31a.c
    ${LIBTDFU_DIR}/src/platforms/t40.c
    ${LIBTDFU_DIR}/src/platforms/t41.c
    ${LIBTDFU_DIR}/src/platforms/a1.c
    ${LIBTDFU_DIR}/src/platform.c
    ${LIBTDFU_DIR}/src/bootstrap.c
    ${LIBTDFU_DIR}/src/utils.c
    ${LIBTDFU_DIR}/src/operations.c
    ${LIBTDFU_DIR}/src/dfu/dfu.c
    # Web glue: provides g_debug_enabled and main()
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.c
)

add_executable(tdfu_web ${LIBTDFU_SOURCES})

target_include_directories(tdfu_web PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${LIBTDFU_DIR}/include
    ${LIBTDFU_DIR}/src
    ${LIBTDFU_DIR}/src/ddr
    ${LIBTDFU_DIR}/src/firmware
    ${LIBTDFU_DIR}/src/flash
)

# Asyncify: write imports list to a JSON file to avoid shell quoting issues
# Only list functions that actually call Asyncify.handleAsync()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "[\"emscripten_sleep\",\"libusb_get_device_list\",\"libusb_open\",\"libusb_set_configuration\",\"libusb_claim_interface\",\"libusb_release_interface\",\"libusb_control_transfer\",\"libusb_bulk_transfer\",\"libusb_interrupt_transfer\",\"libusb_reset_device\"]"
)

# Exported C functions callable from JS
set(EXPORTED_FUNCTIONS
    "_main"
    "_tdfu_init"
    "_tdfu_cleanup"
    "_tdfu_discover_devices"
    "_tdfu_free_device_list"
    "_tdfu_bootstrap"
    "_tdfu_bootstrap_with_data"
    "_tdfu_write_firmware"
    "_tdfu_read_firmware"
    "_tdfu_generate_ddr"
    "_tdfu_set_device_variant"
    "_tdfu_get_version"
    "_tdfu_web_set_debug"
    "_tdfu_variant_to_string"
    "_tdfu_variant_from_string"
    "_tdfu_web_dfu_bootstrap"
    "_tdfu_web_dfu_bootstrap_files"
    "_tdfu_web_detect_soc"
    "_tdfu_web_dfu_download"
    "_tdfu_web_dfu_upload"
    "_malloc"
    "_free"
)
list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCS_STR)

target_compile_options(tdfu_web PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_options(tdfu_web PRIVATE
    "SHELL:-s ASYNCIFY=1"
    "SHELL:-s ASYNCIFY_IMPORTS=@${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCS_STR}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU32\",\"FS\"]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createTdfuModule"
    "SHELL:-s ASYNCIFY_STACK_SIZE=131072"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s INITIAL_MEMORY=16777216"
    "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/libusb-webusb.js"
)

# Output to web/public/wasm/ so Vite treats it as a static asset
set_target_properties(tdfu_web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public/wasm"
    OUTPUT_NAME "tdfu"
    SUFFIX ".js"
    # --js-library is not auto-tracked as a link input, so edits to the shim
    # would otherwise not trigger a relink. Make it an explicit link dependency.
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/libusb-webusb.js"
)
