# BuildLibRaw.cmake — build LibRaw as a static library from the submodule
# without adding any files to the upstream repository.

set(LIBRAW_SRC_DIR "${CMAKE_SOURCE_DIR}/lib/libraw")

file(GLOB_RECURSE LIBRAW_SOURCES "${LIBRAW_SRC_DIR}/src/*.cpp")

# Exclude optional SDK integration files that require external DNG SDK / RawSpeed
list(FILTER LIBRAW_SOURCES EXCLUDE REGEX ".*dngsdk_glue\\.cpp$")
list(FILTER LIBRAW_SOURCES EXCLUDE REGEX ".*rawspeed_glue\\.cpp$")

add_library(raw_static STATIC ${LIBRAW_SOURCES})

# PUBLIC: consumers (rasterlab) get lib/libraw/ on their include path,
# so #include "libraw/libraw.h" resolves to lib/libraw/libraw/libraw.h
target_include_directories(raw_static PUBLIC "${LIBRAW_SRC_DIR}")

target_compile_definitions(raw_static
    PUBLIC  LIBRAW_NODLL
    PRIVATE LIBRAW_BUILDLIB)

# winsock2.h (transitively windows.h) is required for HANDLE, TCHAR, ntohs, etc.
# Link ws2_32 to satisfy ntohs and other winsock symbols used in the RAW decoders.
if(WIN32)
    target_link_libraries(raw_static PRIVATE ws2_32)
endif()

set_target_properties(raw_static PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON)
