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
    ${LIBTDFU_DIR}/src/platform.c
    ${LIBTDFU_DIR}/src/bootstrap.c
    ${LIBTDFU_DIR}/src/utils.c
    ${LIBTDFU_DIR}/src/dfu/dfu.c
    # Web glue: provides g_debug_enabled and main()
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.c
)

add_executable(tdfu_web ${LIBTDFU_SOURCES})

target_include_directories(tdfu_web PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${LIBTDFU_DIR}/include
    ${LIBTDFU_DIR}/src
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
