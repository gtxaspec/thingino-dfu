# libtdfu - core library

set(LIBTDFU_SOURCES
    src/core.c
    src/usb/manager.c
    src/usb/device.c
    src/usb/protocol.c
    src/platform.c
    src/bootstrap.c
    src/utils.c
    src/dfu/dfu.c
)

add_library(tdfu STATIC ${LIBTDFU_SOURCES})

target_include_directories(tdfu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(tdfu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tdfu ${LIBUSB_LIBRARIES})
