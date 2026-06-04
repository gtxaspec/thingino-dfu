# dfu-remote - daemon for remote USB device access over TCP

add_executable(dfu-remote main.c)
target_include_directories(dfu-remote PRIVATE
    ${CMAKE_SOURCE_DIR}/libcloner/src
    ${CMAKE_SOURCE_DIR}/libcloner/src/ddr
    ${CMAKE_SOURCE_DIR}/libcloner/src/firmware
)
if (WIN32)
    target_link_libraries(dfu-remote cloner ws2_32)
else()
    target_link_libraries(dfu-remote cloner z)
endif()
install(TARGETS dfu-remote DESTINATION bin)
