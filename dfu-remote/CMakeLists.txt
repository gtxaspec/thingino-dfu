# dfu-remote - daemon for remote USB device access over TCP

add_executable(dfu-remote main.c ws.c)
target_include_directories(dfu-remote PRIVATE
    ${CMAKE_SOURCE_DIR}/libtdfu/src
)
if (WIN32)
    target_link_libraries(dfu-remote tdfu ws2_32)
else()
    target_link_libraries(dfu-remote tdfu)
endif()
install(TARGETS dfu-remote DESTINATION bin)
