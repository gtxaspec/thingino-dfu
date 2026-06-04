# cloner-cli - command line interface

add_executable(thingino-dfu main.c remote.c)
target_include_directories(thingino-dfu PRIVATE
    ${CMAKE_SOURCE_DIR}/libcloner/src
    ${CMAKE_SOURCE_DIR}/libcloner/src/ddr
    ${CMAKE_SOURCE_DIR}/libcloner/src/firmware
    ${CMAKE_SOURCE_DIR}/libcloner/src/flash
)
if (WIN32)
    target_link_libraries(thingino-dfu cloner ws2_32)
    # Copy libusb-1.0.dll next to the .exe so it can find it at runtime
    if (_VENDORED_LIBUSB_ROOT)
        file(GLOB _libusb_dlls "${_VENDORED_LIBUSB_ROOT}/bin/*.dll")
        foreach(_dll ${_libusb_dlls})
            add_custom_command(TARGET thingino-dfu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:thingino-dfu>")
        endforeach()
    endif()
else()
    target_link_libraries(thingino-dfu cloner z)
endif()
install(TARGETS thingino-dfu DESTINATION bin)
