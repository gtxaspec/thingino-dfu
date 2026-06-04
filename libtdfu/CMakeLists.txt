# libtdfu - core library

set(LIBTDFU_SOURCES
    src/core.c
    src/usb/manager.c
    src/usb/device.c
    src/usb/protocol.c
    src/firmware/loader.c
    src/firmware/reader.c
    src/firmware/writer.c
    src/firmware/handshake.c
    src/firmware/flash_descriptor.c
    src/ddr/ddr_utils.c
    src/ddr/ddr_controller.c
    src/ddr/ddr_param_builder.c
    src/ddr/ddr_phy.c
    src/ddr/ddr_phy_ddr2.c
    src/ddr/ddr_ctrl_txx.c
    src/ddr/ddr_generator.c
    src/ddr/ddr_binary_builder.c
    src/ddr/ddr_rdd_ddr2.c
    src/ddr/ddr_rdd_ddr3.c
    src/ddr/ddr_fidb_xb1.c
    src/ddr/ddr_fidb_xb2.c
    src/ddr/ddr_rdd_xb2.c
    src/ddr/ddr_config_database.c
    src/ddr/ddr_reg_compute.c
    src/flash/spi_nor_db.c
    src/flash/nand_db.c
    src/platforms/platform.c
    src/platforms/t10.c
    src/platforms/t20.c
    src/platforms/t21.c
    src/platforms/t30.c
    src/platforms/t31.c
    src/platforms/t31a.c
    src/platforms/t40.c
    src/platforms/t41.c
    src/platforms/a1.c
    src/platform.c
    src/bootstrap.c
    src/utils.c
    src/operations.c
    src/dfu/dfu.c
)

add_library(tdfu STATIC ${LIBTDFU_SOURCES})

target_include_directories(tdfu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(tdfu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ddr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/firmware
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flash
)

target_link_libraries(tdfu ${LIBUSB_LIBRARIES})
