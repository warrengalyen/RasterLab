cmake_minimum_required(VERSION 3.10)
project(ImageEditor C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set output directory for executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find GTK3, GdkPixbuf, and libxml2 using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0 gdk-pixbuf-2.0)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${LIBXML2_INCLUDE_DIRS})

# Link directories
link_directories(${GTK3_LIBRARY_DIRS})
link_directories(${LIBXML2_LIBRARY_DIRS})

# Add subdirectories
add_subdirectory(src)

# Print configuration summary
message(STATUS "GTK3 found: ${GTK3_LIBRARIES}")
message(STATUS "GTK3 include dirs: ${GTK3_INCLUDE_DIRS}")
message(STATUS "GTK3 library dirs: ${GTK3_LIBRARY_DIRS}")
message(STATUS "GTK3 CFLAGS: ${GTK3_CFLAGS}")
message(STATUS "GTK3 LDFLAGS: ${GTK3_LDFLAGS}")
message(STATUS "GdkPixbuf found: Yes")
