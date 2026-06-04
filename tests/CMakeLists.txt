# Test executables
# All tests link against libtdfu and can access its internal headers

enable_testing()

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/libtdfu/src
    ${CMAKE_SOURCE_DIR}/libtdfu/src/ddr
    ${CMAKE_SOURCE_DIR}/libtdfu/src/firmware
    ${CMAKE_SOURCE_DIR}/libtdfu/src/flash
)

macro(add_tdfu_test name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${name} tdfu)
    add_test(NAME ${name} COMMAND ${name}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endmacro()

# DDR tests (dynamic generation, no reference files needed)
add_tdfu_test(test_ddr_integration)
add_tdfu_test(test_config_database)
add_tdfu_test(test_ddr_reference)

# Flash database test
add_tdfu_test(test_flash_db)

# Network protocol test
add_tdfu_test(test_protocol)

# File operations test
add_tdfu_test(test_file_ops)
