cmake_minimum_required(VERSION 3.10)
project(thingino-cloner LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Version from git tag (e.g. v0.5.5 -> 0.5.5), fallback to "dev"
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_TAG)
    string(REGEX REPLACE "^v" "" PROJECT_VERSION_STR "${GIT_TAG}")
else()
    set(PROJECT_VERSION_STR "dev")
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
add_definitions(-DVERSION="${PROJECT_VERSION_STR}" -DGIT_HASH="${GIT_COMMIT_HASH}")

# Find libusb
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(LIBUSB_PKG libusb-1.0)
endif()

set(LIBUSB_INCLUDE_DIRS "")
set(LIBUSB_LIBRARIES "")

set(_VENDORED_LIBUSB_ROOT "")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows" OR WIN32 OR CMAKE_CROSSCOMPILING)
    set(_libusb_arch "x86_64")
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
        set(_libusb_arch "x86")
    endif()
    set(_vendor_candidate "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb/windows/${_libusb_arch}")
    if (EXISTS "${_vendor_candidate}")
        set(_VENDORED_LIBUSB_ROOT "${_vendor_candidate}")
    endif()
endif()

if (LIBUSB_PKG_FOUND AND NOT CMAKE_CROSSCOMPILING)
    set(LIBUSB_INCLUDE_DIRS ${LIBUSB_PKG_INCLUDE_DIRS})
    if (APPLE AND LIBUSB_PKG_LDFLAGS)
        set(LIBUSB_LIBRARIES ${LIBUSB_PKG_LDFLAGS})
    else()
        set(LIBUSB_LIBRARIES ${LIBUSB_PKG_LIBRARIES})
    endif()
    if (LIBUSB_PKG_LIBRARY_DIRS)
        link_directories(${LIBUSB_PKG_LIBRARY_DIRS})
    endif()
endif()

set(LIBUSB_SEARCH_DIRS)
if (LIBUSB_ROOT)
    list(APPEND LIBUSB_SEARCH_DIRS "${LIBUSB_ROOT}")
endif()
if (_VENDORED_LIBUSB_ROOT)
    list(APPEND LIBUSB_SEARCH_DIRS "${_VENDORED_LIBUSB_ROOT}")
endif()
list(APPEND LIBUSB_SEARCH_DIRS /usr /usr/local /opt/homebrew)
list(REMOVE_DUPLICATES LIBUSB_SEARCH_DIRS)

if (NOT LIBUSB_INCLUDE_DIRS OR NOT LIBUSB_LIBRARIES)
    set(LIBUSB_INCLUDE_HINTS)
    set(LIBUSB_LIBRARY_HINTS)
    foreach(dir IN LISTS LIBUSB_SEARCH_DIRS)
        list(APPEND LIBUSB_INCLUDE_HINTS "${dir}/include" "${dir}/include/libusb-1.0")
        list(APPEND LIBUSB_LIBRARY_HINTS "${dir}/lib" "${dir}/lib64" "${dir}/lib/libusb-1.0" "${dir}/bin")
    endforeach()

    find_path(LIBUSB_INCLUDE_DIR
        NAMES libusb-1.0/libusb.h libusb.h
        HINTS ${LIBUSB_INCLUDE_HINTS})

    find_library(LIBUSB_LIBRARY
        NAMES usb-1.0 libusb-1.0
        HINTS ${LIBUSB_LIBRARY_HINTS})

    if (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
        set(LIBUSB_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIR})
        set(LIBUSB_LIBRARIES ${LIBUSB_LIBRARY})
    endif()
endif()

if (NOT LIBUSB_INCLUDE_DIRS OR NOT LIBUSB_LIBRARIES)
    message(FATAL_ERROR "libusb-1.0 not found. Set LIBUSB_ROOT or install libusb-1.0.")
endif()

# Normalize include dirs
set(_LIBUSB_INCLUDE_DIRS_NORMALIZED)
foreach(dir IN LISTS LIBUSB_INCLUDE_DIRS)
    list(APPEND _LIBUSB_INCLUDE_DIRS_NORMALIZED "${dir}")
    get_filename_component(_libusb_dir_name "${dir}" NAME)
    if (_libusb_dir_name STREQUAL "libusb-1.0")
        get_filename_component(_libusb_parent "${dir}" DIRECTORY)
        list(APPEND _LIBUSB_INCLUDE_DIRS_NORMALIZED "${_libusb_parent}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _LIBUSB_INCLUDE_DIRS_NORMALIZED)
set(LIBUSB_INCLUDE_DIRS ${_LIBUSB_INCLUDE_DIRS_NORMALIZED})

# On macOS, static libusb needs IOKit and CoreFoundation frameworks
if (APPLE)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(SECURITY_FRAMEWORK Security)
    list(APPEND LIBUSB_LIBRARIES ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${SECURITY_FRAMEWORK})
endif()

# Global settings
include_directories(${LIBUSB_INCLUDE_DIRS})
add_compile_options(-Wall -Wextra)
if (LIBUSB_PKG_FOUND AND LIBUSB_PKG_CFLAGS_OTHER)
    add_compile_options(${LIBUSB_PKG_CFLAGS_OTHER})
endif()

# Enable CTest
enable_testing()

# Build targets
add_subdirectory(libcloner)
add_subdirectory(cli)
add_subdirectory(cloner-remote)
add_subdirectory(tests)

# Install targets
# Binaries and firmware install to the same directory so that the binary
# can find firmware/ relative to its own location via /proc/self/exe.
# Default: /usr/local/lib/thingino-cloner/  (override with -DCLONER_INSTALL_DIR=)
set(CLONER_INSTALL_DIR "lib/thingino-cloner" CACHE STRING "Install directory for cloner binaries and firmware")
install(TARGETS thingino-cloner cloner-remote RUNTIME DESTINATION ${CLONER_INSTALL_DIR})
install(DIRECTORY firmware/ DESTINATION ${CLONER_INSTALL_DIR}/firmware)

# Symlinks in bin/ for convenience
install(CODE "
    file(MAKE_DIRECTORY \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin)
    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
        ../${CLONER_INSTALL_DIR}/thingino-cloner
        \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/thingino-cloner)
    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
        ../${CLONER_INSTALL_DIR}/cloner-remote
        \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/cloner-remote)
")
