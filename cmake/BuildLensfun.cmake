# BuildLensfun.cmake — build lensfun as a static library from the submodule
# without invoking its upstream CMakeLists.txt (which has complex dependencies).

set(LENSFUN_SRC_DIR "${CMAKE_SOURCE_DIR}/lib/lensfun")
set(LENSFUN_BUILD_DIR "${CMAKE_BINARY_DIR}/lensfun")

# Version numbers from upstream
set(VERSION_MAJOR 0)
set(VERSION_MINOR 3)
set(VERSION_MICRO 99)
set(VERSION_BUGFIX 0)
set(LENSFUN_DB_VERSION 2)

# GLib version requirement (no tests, so 2.26 is sufficient)
set(LENSFUN_GLIB_REQUIREMENT_MACRO "GLIB_VERSION_2_26")

# Check for endian.h
include(CheckIncludeFiles)
check_include_files(endian.h HAVE_ENDIAN_H)

# SSE support on x86
if(CMAKE_SYSTEM_PROCESSOR MATCHES "[XxIi][0-9]?86|[Aa][Mm][Dd]64")
    set(VECTORIZATION_SSE 1)
    set(VECTORIZATION_SSE2 1)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(VECTORIZATION_SSE_FLAGS "-msse")
        set(VECTORIZATION_SSE2_FLAGS "-msse2")
    endif()
endif()

file(MAKE_DIRECTORY "${LENSFUN_BUILD_DIR}")

# Generate config.h and lensfun.h from templates
configure_file(
    "${LENSFUN_SRC_DIR}/include/lensfun/config.h.in.cmake"
    "${LENSFUN_BUILD_DIR}/config.h"
)
configure_file(
    "${LENSFUN_SRC_DIR}/include/lensfun/lensfun.h.in"
    "${LENSFUN_BUILD_DIR}/lensfun.h"
    @ONLY
)

# Collect sources
set(LENSFUN_SOURCES
    "${LENSFUN_SRC_DIR}/libs/lensfun/auxfun.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/camera.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/cpuid.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/database.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/lens.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-color.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-color-sse.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-color-sse2.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-coord.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-coord-sse.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-pc.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mod-subpix.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/modifier.cpp"
    "${LENSFUN_SRC_DIR}/libs/lensfun/mount.cpp"
)

# SSE compile flags for specific files
if(VECTORIZATION_SSE_FLAGS)
    set_source_files_properties(
        "${LENSFUN_SRC_DIR}/libs/lensfun/mod-color-sse.cpp"
        "${LENSFUN_SRC_DIR}/libs/lensfun/mod-coord-sse.cpp"
        PROPERTIES COMPILE_FLAGS "${VECTORIZATION_SSE_FLAGS}"
    )
endif()
if(VECTORIZATION_SSE2_FLAGS)
    set_source_files_properties(
        "${LENSFUN_SRC_DIR}/libs/lensfun/mod-color-sse2.cpp"
        PROPERTIES COMPILE_FLAGS "${VECTORIZATION_SSE2_FLAGS}"
    )
endif()

add_library(lensfun_static STATIC ${LENSFUN_SOURCES})

# Generated headers (config.h, lensfun.h) live in the build dir
target_include_directories(lensfun_static
    PUBLIC  "${LENSFUN_BUILD_DIR}"
    PUBLIC  "${LENSFUN_SRC_DIR}/include/lensfun"
    PRIVATE "${LENSFUN_SRC_DIR}/libs/lensfun"
)

target_compile_definitions(lensfun_static
    PUBLIC  CONF_LENSFUN_STATIC
    PRIVATE CONF_LENSFUN_INTERNAL
)

# GLib2 (already available via GTK3 pkg-config in the parent project)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GLIB2 QUIET glib-2.0)
endif()
if(GLIB2_FOUND)
    target_include_directories(lensfun_static PRIVATE ${GLIB2_INCLUDE_DIRS})
    target_link_libraries(lensfun_static PRIVATE ${GLIB2_LIBRARIES})
else()
    message(FATAL_ERROR "GLib2 is required to build lensfun (normally provided by GTK3)")
endif()

set_target_properties(lensfun_static PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
