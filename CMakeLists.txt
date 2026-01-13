cmake_minimum_required(VERSION 3.10)
project(RasterLab C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set output directory for executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# Include ExternalProject module for building external libraries
include(ExternalProject)

# Check for zlib - prefer building from source submodule (must be before libpng)
set(ZLIB_FOUND FALSE)
set(ZLIB_LIBRARIES "")
set(ZLIB_TARGET "")

# Check for libjpeg - prefer building from source submodule
set(LIBJPEG_FOUND FALSE)
set(LIBJPEG_LIBRARIES "")
set(LIBJPEG_TARGET "")

# Include ExternalProject module for building libjpeg-turbo
include(ExternalProject)

# Check if libjpeg-turbo submodule exists
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libjpeg/CMakeLists.txt")
    message(STATUS "libjpeg-turbo submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(LIBJPEG_BUILD_DIR "${CMAKE_BINARY_DIR}/libjpeg_build")
    set(LIBJPEG_INSTALL_DIR "${CMAKE_BINARY_DIR}/libjpeg_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows with libjpeg-turbo, the DLL is named libjpeg-8.dll
        set(LIBJPEG_DLL_NAME "libjpeg-8.dll")
        set(LIBJPEG_DLL_PATH "${LIBJPEG_INSTALL_DIR}/bin/${LIBJPEG_DLL_NAME}")
        
        # For MinGW, the import library is libjpeg.dll.a
        # For MSVC, it would be jpeg.lib (but we're using MinGW here)
        if(MINGW)
            set(LIBJPEG_IMPLIB_NAME "libjpeg.dll.a")
        else()
            set(LIBJPEG_IMPLIB_NAME "jpeg.lib")
        endif()
        set(LIBJPEG_IMPLIB_PATH "${LIBJPEG_INSTALL_DIR}/lib/${LIBJPEG_IMPLIB_NAME}")
    else()
        # On Unix-like systems, shared library is in lib/
        set(LIBJPEG_LIB_NAME "libjpeg${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBJPEG_LIB_PATH "${LIBJPEG_INSTALL_DIR}/lib/${LIBJPEG_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for libjpeg-turbo
    # Only build shared library, not static
    set(LIBJPEG_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBJPEG_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DENABLE_SHARED=ON
        -DENABLE_STATIC=OFF
        -DWITH_JPEG8=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=ON
    )
    
    # Pass architecture-specific settings
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        list(APPEND LIBJPEG_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        # 32-bit
        list(APPEND LIBJPEG_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    # Add ExternalProject to build libjpeg-turbo
    # ExternalProject_Add will use default build/install commands that handle multi-config generators correctly
    ExternalProject_Add(
        libjpeg_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libjpeg"
        BINARY_DIR "${LIBJPEG_BUILD_DIR}"
        INSTALL_DIR "${LIBJPEG_INSTALL_DIR}"
        CMAKE_ARGS ${LIBJPEG_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )
    
    # Set include directory - use source directory for headers during configure
    # The installed headers will be available at build time
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libjpeg/include")
        set(LIBJPEG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libjpeg/include")
    else()
        # Fallback to source directory root
        set(LIBJPEG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libjpeg")
    endif()
    
    include_directories(${LIBJPEG_INCLUDE_DIRS})
    
    # Create imported target for linking
    add_library(jpeg SHARED IMPORTED GLOBAL)
    
    # Set properties
    if(WIN32)
        # On Windows, set the DLL location and import library
        set_target_properties(jpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEG_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBJPEG_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBJPEG_IMPLIB_PATH}"
        )
        
        # Copy DLL to bin directory at build time
        add_custom_command(TARGET libjpeg_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBJPEG_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libjpeg DLL to bin directory"
        )
    else()
        # On Unix-like systems, set the shared library location
        set_target_properties(jpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEG_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBJPEG_LIB_PATH}"
        )
        
        # Copy shared library to bin directory at build time
        add_custom_command(TARGET libjpeg_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBJPEG_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libjpeg shared library to bin directory"
        )
    endif()
    
    set(LIBJPEG_FOUND TRUE)
    set(LIBJPEG_TARGET jpeg)
    
    message(STATUS "Building libjpeg-turbo from source as shared library")
    message(STATUS "libjpeg include dirs: ${LIBJPEG_INCLUDE_DIRS}")
    if(WIN32)
        message(STATUS "libjpeg DLL path: ${LIBJPEG_DLL_PATH}")
        message(STATUS "libjpeg import library path: ${LIBJPEG_IMPLIB_PATH}")
    else()
        message(STATUS "libjpeg library path: ${LIBJPEG_LIB_PATH}")
    endif()
else()
    # Fallback to system libjpeg via pkg-config
    message(STATUS "libjpeg submodule not found, trying system libjpeg")
    pkg_check_modules(LIBJPEG libjpeg)
    
    if(LIBJPEG_FOUND)
        include_directories(${LIBJPEG_INCLUDE_DIRS})
        message(STATUS "Using system libjpeg: ${LIBJPEG_INCLUDE_DIRS}")
    else()
        message(WARNING "libjpeg not found. JPEG plugin will not be available.")
        message(WARNING "To add libjpeg as submodule, run:")
        message(WARNING "  git submodule add https://github.com/libjpeg-turbo/libjpeg-turbo.git lib/libjpeg")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

if(LIBJPEG_FOUND)
    set(HAVE_LIBJPEG 1)
else()
    set(HAVE_LIBJPEG 0)
endif()

# Check if zlib submodule exists
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/zlib/CMakeLists.txt")
    message(STATUS "zlib submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(ZLIB_BUILD_DIR "${CMAKE_BINARY_DIR}/zlib_build")
    set(ZLIB_INSTALL_DIR "${CMAKE_BINARY_DIR}/zlib_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows, zlib outputs zlib1.dll (versioned)
        # Try both possible names as zlib CMake can output different names
        set(ZLIB_DLL_NAME "zlib1.dll")
        set(ZLIB_DLL_PATH "${ZLIB_INSTALL_DIR}/bin/${ZLIB_DLL_NAME}")
        
        # For MinGW, zlib creates libz.dll.a (not zlib.dll.a)
        # For MSVC, it would be zlib.lib
        if(MINGW)
            set(ZLIB_IMPLIB_NAME "libz.dll.a")
        else()
            set(ZLIB_IMPLIB_NAME "zlib.lib")
        endif()
        set(ZLIB_IMPLIB_PATH "${ZLIB_INSTALL_DIR}/lib/${ZLIB_IMPLIB_NAME}")
    else()
        # On Unix-like systems, shared library is in lib/
        set(ZLIB_LIB_NAME "libz${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(ZLIB_LIB_PATH "${ZLIB_INSTALL_DIR}/lib/${ZLIB_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for zlib
    # Only build shared library, not static
    set(ZLIB_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${ZLIB_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    
    # Pass architecture-specific settings
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        list(APPEND ZLIB_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        # 32-bit
        list(APPEND ZLIB_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    # Add ExternalProject to build zlib
    ExternalProject_Add(
        zlib_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/zlib"
        BINARY_DIR "${ZLIB_BUILD_DIR}"
        INSTALL_DIR "${ZLIB_INSTALL_DIR}"
        CMAKE_ARGS ${ZLIB_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )
    
    # Set include directory - use source directory for headers during configure
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/zlib/include")
        set(ZLIB_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/zlib/include")
    else()
        # Fallback to source directory root
        set(ZLIB_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/zlib")
    endif()
    
    include_directories(${ZLIB_INCLUDE_DIRS})
    
    # Create imported target for linking
    add_library(zlib SHARED IMPORTED GLOBAL)
    
    # Set properties
    if(WIN32)
        # On Windows, set the DLL location and import library
        set_target_properties(zlib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${ZLIB_DLL_PATH}"
            IMPORTED_IMPLIB "${ZLIB_IMPLIB_PATH}"
        )
        
        # Copy DLL to bin directory at build time
        add_custom_command(TARGET zlib_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ZLIB_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying zlib DLL to bin directory"
        )
    else()
        # On Unix-like systems, set the shared library location
        set_target_properties(zlib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${ZLIB_LIB_PATH}"
        )
        
        # Copy shared library to bin directory at build time
        add_custom_command(TARGET zlib_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ZLIB_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying zlib shared library to bin directory"
        )
    endif()
    
    set(ZLIB_FOUND TRUE)
    set(ZLIB_TARGET zlib)
    
    message(STATUS "Building zlib from source as shared library")
    message(STATUS "zlib include dirs: ${ZLIB_INCLUDE_DIRS}")
    if(WIN32)
        message(STATUS "zlib DLL path: ${ZLIB_DLL_PATH}")
        message(STATUS "zlib import library path: ${ZLIB_IMPLIB_PATH}")
    else()
        message(STATUS "zlib library path: ${ZLIB_LIB_PATH}")
    endif()
else()
    # Fallback to system zlib via pkg-config
    message(STATUS "zlib submodule not found, trying system zlib")
    pkg_check_modules(ZLIB zlib)
    
    if(NOT ZLIB_FOUND)
        # Try to find zlib using CMake's built-in find
        find_package(ZLIB QUIET)
    endif()
    
    if(ZLIB_FOUND OR ZLIB_LIBRARIES)
        include_directories(${ZLIB_INCLUDE_DIRS})
        message(STATUS "Using system zlib: ${ZLIB_INCLUDE_DIRS}")
    else()
        message(WARNING "zlib not found. libpng requires zlib as a dependency.")
        message(WARNING "To add zlib as submodule, run:")
        message(WARNING "  git submodule add https://github.com/madler/zlib.git lib/zlib")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

# Check if libpng submodule exists
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libpng/CMakeLists.txt")
    message(STATUS "libpng submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(LIBPNG_BUILD_DIR "${CMAKE_BINARY_DIR}/libpng_build")
    set(LIBPNG_INSTALL_DIR "${CMAKE_BINARY_DIR}/libpng_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows, libpng 1.6.x outputs libpng16.dll
        set(LIBPNG_DLL_NAME "libpng16.dll")
        set(LIBPNG_DLL_PATH "${LIBPNG_INSTALL_DIR}/bin/${LIBPNG_DLL_NAME}")
        
        # For MinGW, the import library is libpng16.dll.a
        if(MINGW)
            set(LIBPNG_IMPLIB_NAME "libpng16.dll.a")
        else()
            set(LIBPNG_IMPLIB_NAME "libpng16.lib")
        endif()
        set(LIBPNG_IMPLIB_PATH "${LIBPNG_INSTALL_DIR}/lib/${LIBPNG_IMPLIB_NAME}")
    else()
        # On Unix-like systems, shared library is in lib/
        set(LIBPNG_LIB_NAME "libpng16${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBPNG_LIB_PATH "${LIBPNG_INSTALL_DIR}/lib/${LIBPNG_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for libpng
    # Only build shared library, not static
    set(LIBPNG_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBPNG_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DPNG_SHARED=ON
        -DPNG_STATIC=OFF
        -DPNG_TESTS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    
    # Pass zlib information to libpng if we're building zlib from source
    if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
        # Pass zlib paths to libpng so it uses our built zlib
        # libpng's CMake uses find_package(ZLIB), so we set CMAKE_PREFIX_PATH
        # to help it find the zlib we built via its CMake config files
        # We also pass explicit paths as a fallback
        if(WIN32)
            list(APPEND LIBPNG_CMAKE_ARGS 
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_IMPLIB_PATH}
                -DCMAKE_PREFIX_PATH=${ZLIB_INSTALL_DIR}
            )
        else()
            list(APPEND LIBPNG_CMAKE_ARGS 
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_LIB_PATH}
                -DCMAKE_PREFIX_PATH=${ZLIB_INSTALL_DIR}
            )
        endif()
    endif()
    
    # Pass architecture-specific settings
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        list(APPEND LIBPNG_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        # 32-bit
        list(APPEND LIBPNG_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    # Add ExternalProject to build libpng
    # If we're building zlib from source, make libpng depend on it
    # ExternalProject_Add automatically handles CMake build/install commands
    if(ZLIB_TARGET AND TARGET zlib_external)
        ExternalProject_Add(
            libpng_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libpng"
            BINARY_DIR "${LIBPNG_BUILD_DIR}"
            CMAKE_ARGS ${LIBPNG_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
            DEPENDS zlib_external
        )
    else()
        ExternalProject_Add(
            libpng_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libpng"
            BINARY_DIR "${LIBPNG_BUILD_DIR}"
            CMAKE_ARGS ${LIBPNG_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
        )
    endif()
    
    # Set include directory - use source directory headers for configuration
    # The installed headers will be available at build time after ExternalProject builds
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libpng/include")
        set(LIBPNG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libpng/include")
    else()
        # Fallback to source directory root
        set(LIBPNG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libpng")
    endif()
    
    # Create imported target for linking
    add_library(png SHARED IMPORTED GLOBAL)
    
    # Set properties - Use source directory headers for configuration
    # At build time, the ExternalProject will ensure the installed headers match
    if(WIN32)
        # On Windows, set the DLL location and import library
        set_target_properties(png PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBPNG_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBPNG_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBPNG_IMPLIB_PATH}"
        )
        
        # Copy DLL to bin directory after install completes
        # Use a CMake script to handle cases where DLL might not be installed
        set(LIBPNG_BUILD_DIR_VAR "${LIBPNG_BUILD_DIR}")
        configure_file(
            "${CMAKE_SOURCE_DIR}/cmake/CopyLibpngDLL.cmake"
            "${CMAKE_BINARY_DIR}/cmake/CopyLibpngDLL.cmake"
            @ONLY
        )
        add_custom_command(TARGET libpng_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/cmake/CopyLibpngDLL.cmake"
            COMMENT "Copying libpng DLL to bin directory"
        )
    else()
        # On Unix-like systems, set the shared library location
        set_target_properties(png PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBPNG_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBPNG_LIB_PATH}"
        )
        
        # Copy shared library to bin directory at build time
        add_custom_command(TARGET libpng_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBPNG_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libpng shared library to bin directory"
        )
    endif()
    
    set(LIBPNG_FOUND TRUE)
    set(LIBPNG_TARGET png)
    
    message(STATUS "Building libpng from source as shared library")
    message(STATUS "libpng include dirs: ${LIBPNG_INCLUDE_DIRS}")
    if(WIN32)
        message(STATUS "libpng DLL path: ${LIBPNG_DLL_PATH}")
        message(STATUS "libpng import library path: ${LIBPNG_IMPLIB_PATH}")
    else()
        message(STATUS "libpng library path: ${LIBPNG_LIB_PATH}")
    endif()
else()
    # Fallback to system libpng via pkg-config
    message(STATUS "libpng submodule not found, trying system libpng")
    pkg_check_modules(LIBPNG libpng16)
    
    if(NOT LIBPNG_FOUND)
        pkg_check_modules(LIBPNG libpng)
    endif()
    
    if(LIBPNG_FOUND)
        include_directories(${LIBPNG_INCLUDE_DIRS})
        message(STATUS "Using system libpng: ${LIBPNG_INCLUDE_DIRS}")
    else()
        message(WARNING "libpng not found. PNG plugin will not be available.")
        message(WARNING "To add libpng as submodule, run:")
        message(WARNING "  git submodule add https://github.com/glennrp/libpng.git lib/libpng")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

if(LIBPNG_FOUND)
    set(HAVE_LIBPNG 1)
else()
    set(HAVE_LIBPNG 0)
endif()

# Find GTK3, GdkPixbuf, libxml2, and LZ4 using pkg-config
# NOTE: This is done AFTER zlib/libpng configuration to ensure our libpng headers
# are set up before GTK3's pkg-config adds system libpng headers to CFLAGS
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0 gdk-pixbuf-2.0)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(LZ4 REQUIRED liblz4)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${LIBXML2_INCLUDE_DIRS})
include_directories(${LZ4_INCLUDE_DIRS})

# Link directories
link_directories(${GTK3_LIBRARY_DIRS})
link_directories(${LIBXML2_LIBRARY_DIRS})
link_directories(${LZ4_LIBRARY_DIRS})

# Add subdirectories
add_subdirectory(src)

# Print configuration summary
message(STATUS "GTK3 found: ${GTK3_LIBRARIES}")
message(STATUS "GTK3 include dirs: ${GTK3_INCLUDE_DIRS}")
message(STATUS "GTK3 library dirs: ${GTK3_LIBRARY_DIRS}")
message(STATUS "GTK3 CFLAGS: ${GTK3_CFLAGS}")
message(STATUS "GTK3 LDFLAGS: ${GTK3_LDFLAGS}")
message(STATUS "GdkPixbuf found: Yes")
