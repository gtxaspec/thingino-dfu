cmake_minimum_required(VERSION 3.10)
project(thingino-cloner-web LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Version
add_definitions(-DVERSION="web-dev" -DGIT_HASH="wasm")

# Global debug flag (defined in our web glue)
# The library references g_debug_enabled; we define it in web_main.c

# Use our stub libusb header (functions implemented by JS shim)
set(LIBUSB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/libusb-stub")

# Pull in all libcloner sources
set(LIBCLONER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libcloner")

set(LIBCLONER_SOURCES
    ${LIBCLONER_DIR}/src/core.c
    ${LIBCLONER_DIR}/src/usb/manager.c
    ${LIBCLONER_DIR}/src/usb/device.c
    ${LIBCLONER_DIR}/src/usb/protocol.c
    ${LIBCLONER_DIR}/src/firmware/loader.c
    ${LIBCLONER_DIR}/src/firmware/reader.c
    ${LIBCLONER_DIR}/src/firmware/writer.c
    ${LIBCLONER_DIR}/src/firmware/handshake.c
    ${LIBCLONER_DIR}/src/firmware/flash_descriptor.c
    ${LIBCLONER_DIR}/src/ddr/ddr_utils.c
    ${LIBCLONER_DIR}/src/ddr/ddr_controller.c
    ${LIBCLONER_DIR}/src/ddr/ddr_param_builder.c
    ${LIBCLONER_DIR}/src/ddr/ddr_phy.c
    ${LIBCLONER_DIR}/src/ddr/ddr_phy_ddr2.c
    ${LIBCLONER_DIR}/src/ddr/ddr_ctrl_txx.c
    ${LIBCLONER_DIR}/src/ddr/ddr_generator.c
    ${LIBCLONER_DIR}/src/ddr/ddr_binary_builder.c
    ${LIBCLONER_DIR}/src/ddr/ddr_rdd_ddr2.c
    ${LIBCLONER_DIR}/src/ddr/ddr_rdd_ddr3.c
    ${LIBCLONER_DIR}/src/ddr/ddr_fidb_xb1.c
    ${LIBCLONER_DIR}/src/ddr/ddr_fidb_xb2.c
    ${LIBCLONER_DIR}/src/ddr/ddr_rdd_xb2.c
    ${LIBCLONER_DIR}/src/ddr/ddr_config_database.c
    ${LIBCLONER_DIR}/src/ddr/ddr_reg_compute.c
    ${LIBCLONER_DIR}/src/flash/spi_nor_db.c
    ${LIBCLONER_DIR}/src/flash/nand_db.c
    ${LIBCLONER_DIR}/src/platforms/platform.c
    ${LIBCLONER_DIR}/src/platforms/t10.c
    ${LIBCLONER_DIR}/src/platforms/t20.c
    ${LIBCLONER_DIR}/src/platforms/t21.c
    ${LIBCLONER_DIR}/src/platforms/t30.c
    ${LIBCLONER_DIR}/src/platforms/t31.c
    ${LIBCLONER_DIR}/src/platforms/t31a.c
    ${LIBCLONER_DIR}/src/platforms/t40.c
    ${LIBCLONER_DIR}/src/platforms/t41.c
    ${LIBCLONER_DIR}/src/platforms/a1.c
    ${LIBCLONER_DIR}/src/platform.c
    ${LIBCLONER_DIR}/src/bootstrap.c
    ${LIBCLONER_DIR}/src/utils.c
    ${LIBCLONER_DIR}/src/operations.c
    # Web glue: provides g_debug_enabled and main()
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.c
)

add_executable(cloner_web ${LIBCLONER_SOURCES})

target_include_directories(cloner_web PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${LIBCLONER_DIR}/include
    ${LIBCLONER_DIR}/src
    ${LIBCLONER_DIR}/src/ddr
    ${LIBCLONER_DIR}/src/firmware
    ${LIBCLONER_DIR}/src/flash
)

# Asyncify: write imports list to a JSON file to avoid shell quoting issues
# Only list functions that actually call Asyncify.handleAsync()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "[\"emscripten_sleep\",\"libusb_get_device_list\",\"libusb_open\",\"libusb_set_configuration\",\"libusb_claim_interface\",\"libusb_release_interface\",\"libusb_control_transfer\",\"libusb_bulk_transfer\",\"libusb_interrupt_transfer\",\"libusb_reset_device\"]"
)

# Exported C functions callable from JS
set(EXPORTED_FUNCTIONS
    "_main"
    "_cloner_init"
    "_cloner_cleanup"
    "_cloner_discover_devices"
    "_cloner_free_device_list"
    "_cloner_bootstrap"
    "_cloner_bootstrap_with_data"
    "_cloner_write_firmware"
    "_cloner_read_firmware"
    "_cloner_generate_ddr"
    "_cloner_set_device_variant"
    "_cloner_variant_to_string"
    "_cloner_variant_from_string"
    "_malloc"
    "_free"
)
list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCS_STR)

target_compile_options(cloner_web PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_options(cloner_web PRIVATE
    "SHELL:-s ASYNCIFY=1"
    "SHELL:-s ASYNCIFY_IMPORTS=@${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCS_STR}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU32\",\"FS\"]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createClonerModule"
    "SHELL:-s ASYNCIFY_STACK_SIZE=131072"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s INITIAL_MEMORY=16777216"
    "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/libusb-webusb.js"
)

# Output to web/public/wasm/ so Vite treats it as a static asset
set_target_properties(cloner_web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public/wasm"
    OUTPUT_NAME "cloner"
    SUFFIX ".js"
)
