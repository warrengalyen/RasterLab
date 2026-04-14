cmake_minimum_required(VERSION 3.10)
project(RasterLab C)

# C++ toolchain (used by ExternalProject builds such as libjxl); keeps CMAKE_CXX_COMPILER defined for subprojects
enable_language(CXX)

# Option: build only the application, use system libs (skip building libjpeg, libpng, libtiff, libwebp, libheif, libde265, libaom, zlib, openexr from lib/)
option(RASTERLAB_APP_ONLY "Only build the application; skip building libraries in lib/ (use system libjpeg, libpng, libtiff, libwebp, libheif, libde265, libaom, zlib, openexr, libjxl)" OFF)
if(RASTERLAB_APP_ONLY)
    message(STATUS "RASTERLAB_APP_ONLY=ON: skipping build of lib/ deps, using system libraries")
endif()

# libjxl: default to a smaller shared library on 64-bit x86 (fewer SIMD backends, MinSizeRel, strip on MinGW)
option(RASTERLAB_LIBJXL_SMALL "Prefer smaller libjxl (fewer Highway targets on x86_64, MinSizeRel when not Debug, strip MinGW DLL)" ON)

# PkgConfig needed early when using system libs (APP_ONLY or missing submodules)
find_package(PkgConfig REQUIRED)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Use -O3 for Release builds (default is often -O2)
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

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

# Check if libjpeg-turbo submodule exists (skip when APP_ONLY: use system libs)
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libjpeg/CMakeLists.txt")
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

# Check if zlib submodule exists (skip when APP_ONLY: use system libs)
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/zlib/CMakeLists.txt")
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

# Check if libpng submodule exists (skip when APP_ONLY: use system libs)
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libpng/CMakeLists.txt")
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

# Check if libwebp submodule exists
set(LIBWEBP_FOUND FALSE)
set(LIBWEBP_LIBRARIES "")
set(LIBWEBP_TARGET "")

# Skip building libwebp from source when APP_ONLY
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libwebp/CMakeLists.txt")
    message(STATUS "libwebp submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(LIBWEBP_BUILD_DIR "${CMAKE_BINARY_DIR}/libwebp_build")
    set(LIBWEBP_INSTALL_DIR "${CMAKE_BINARY_DIR}/libwebp_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows, libwebp outputs libwebp.dll
        set(LIBWEBP_DLL_NAME "libwebp.dll")
        set(LIBWEBP_DLL_PATH "${LIBWEBP_INSTALL_DIR}/bin/${LIBWEBP_DLL_NAME}")
        
        # libwebpdemux outputs libwebpdemux.dll
        set(LIBWEBPDEMUX_DLL_NAME "libwebpdemux.dll")
        set(LIBWEBPDEMUX_DLL_PATH "${LIBWEBP_INSTALL_DIR}/bin/${LIBWEBPDEMUX_DLL_NAME}")

        # libwebpmux outputs libwebpmux.dll
        set(LIBWEBPMUX_DLL_NAME "libwebpmux.dll")
        set(LIBWEBPMUX_DLL_PATH "${LIBWEBP_INSTALL_DIR}/bin/${LIBWEBPMUX_DLL_NAME}")

        # For MinGW, the import library is libwebp.dll.a
        if(MINGW)
            set(LIBWEBP_IMPLIB_NAME "libwebp.dll.a")
            set(LIBWEBPDEMUX_IMPLIB_NAME "libwebpdemux.dll.a")
            set(LIBWEBPMUX_IMPLIB_NAME "libwebpmux.dll.a")
        else()
            set(LIBWEBP_IMPLIB_NAME "webp.lib")
            set(LIBWEBPDEMUX_IMPLIB_NAME "webpdemux.lib")
            set(LIBWEBPMUX_IMPLIB_NAME "webpmux.lib")
        endif()
        set(LIBWEBP_IMPLIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBP_IMPLIB_NAME}")
        set(LIBWEBPDEMUX_IMPLIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBPDEMUX_IMPLIB_NAME}")
        set(LIBWEBPMUX_IMPLIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBPMUX_IMPLIB_NAME}")
    else()
        # On Unix-like systems, shared library is in lib/
        set(LIBWEBP_LIB_NAME "libwebp${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBWEBP_LIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBP_LIB_NAME}")
        set(LIBWEBPDEMUX_LIB_NAME "libwebpdemux${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBWEBPDEMUX_LIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBPDEMUX_LIB_NAME}")
        set(LIBWEBPMUX_LIB_NAME "libwebpmux${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBWEBPMUX_LIB_PATH "${LIBWEBP_INSTALL_DIR}/lib/${LIBWEBPMUX_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for libwebp
    # Only build shared library, not static
    set(LIBWEBP_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBWEBP_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DWEBP_BUILD_ANIM_UTILS=OFF
        -DWEBP_BUILD_CWEBP=OFF
        -DWEBP_BUILD_DWEBP=OFF
        -DWEBP_BUILD_GIF2WEBP=OFF
        -DWEBP_BUILD_IMG2WEBP=OFF
        -DWEBP_BUILD_VWEBP=OFF
        -DWEBP_BUILD_WEBPINFO=OFF
        -DWEBP_BUILD_WEBPMUX=ON
        -DWEBP_BUILD_EXTRAS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    
    # Pass architecture-specific settings
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        list(APPEND LIBWEBP_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        # 32-bit
        list(APPEND LIBWEBP_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    # Add ExternalProject to build libwebp
    ExternalProject_Add(
        libwebp_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libwebp"
        BINARY_DIR "${LIBWEBP_BUILD_DIR}"
        INSTALL_DIR "${LIBWEBP_INSTALL_DIR}"
        CMAKE_ARGS ${LIBWEBP_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )
    
    # Set include directory - use source directory for headers during configure
    # The installed headers will be available at build time
    # libwebp headers are in src/webp/ subdirectory, so we point to src/
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libwebp/src")
        set(LIBWEBP_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libwebp/src")
    else()
        # Fallback to source directory root
        set(LIBWEBP_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libwebp")
    endif()
    
    include_directories(${LIBWEBP_INCLUDE_DIRS})
    
    # Create imported target for linking
    add_library(webp SHARED IMPORTED GLOBAL)
    add_library(webpdemux SHARED IMPORTED GLOBAL)
    add_library(webpmux SHARED IMPORTED GLOBAL)
    
    # Set properties
    if(WIN32)
        # On Windows, set the DLL location and import library
        set_target_properties(webp PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBP_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBWEBP_IMPLIB_PATH}"
        )
        
        set_target_properties(webpdemux PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBPDEMUX_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBWEBPDEMUX_IMPLIB_PATH}"
        )

        set_target_properties(webpmux PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBPMUX_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBWEBPMUX_IMPLIB_PATH}"
        )
        
        # Copy DLLs to bin directory at build time
        add_custom_command(TARGET libwebp_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBP_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBPDEMUX_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBPMUX_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libwebp DLLs to bin directory"
        )
    else()
        # On Unix-like systems, set the shared library location
        set_target_properties(webp PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBP_LIB_PATH}"
        )
        
        set_target_properties(webpdemux PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBPDEMUX_LIB_PATH}"
        )

        set_target_properties(webpmux PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBP_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBWEBPMUX_LIB_PATH}"
        )
        
        # Copy shared libraries to bin directory at build time
        add_custom_command(TARGET libwebp_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBP_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBPDEMUX_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBWEBPMUX_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libwebp shared libraries to bin directory"
        )
    endif()
    
    # Set dependency: webpdemux and webpmux depend on webp
    set_target_properties(webpdemux PROPERTIES
        INTERFACE_LINK_LIBRARIES webp
    )
    set_target_properties(webpmux PROPERTIES
        INTERFACE_LINK_LIBRARIES webp
    )
    
    set(LIBWEBP_FOUND TRUE)
    set(LIBWEBP_TARGET webp)
    
    message(STATUS "Building libwebp from source as shared library")
    message(STATUS "libwebp include dirs: ${LIBWEBP_INCLUDE_DIRS}")
    if(WIN32)
        message(STATUS "libwebp DLL path: ${LIBWEBP_DLL_PATH}")
        message(STATUS "libwebp import library path: ${LIBWEBP_IMPLIB_PATH}")
    else()
        message(STATUS "libwebp library path: ${LIBWEBP_LIB_PATH}")
    endif()
else()
    # Fallback to system libwebp via pkg-config
    message(STATUS "libwebp submodule not found, trying system libwebp")
    pkg_check_modules(LIBWEBP libwebp)
    pkg_check_modules(LIBWEBPDEMUX libwebpdemux)
    pkg_check_modules(LIBWEBPMUX libwebpmux)
    
    if(LIBWEBP_FOUND)
        include_directories(${LIBWEBP_INCLUDE_DIRS})
        message(STATUS "Using system libwebp: ${LIBWEBP_INCLUDE_DIRS}")
        if(LIBWEBPDEMUX_FOUND)
            message(STATUS "Using system libwebpdemux: ${LIBWEBPDEMUX_LIBRARIES}")
        else()
            message(WARNING "libwebpdemux not found. Animated WebP support may not work.")
        endif()
        if(LIBWEBPMUX_FOUND)
            message(STATUS "Using system libwebpmux: ${LIBWEBPMUX_LIBRARIES}")
        else()
            message(WARNING "libwebpmux not found. WebP ICC embedding may not work.")
        endif()
    else()
        message(WARNING "libwebp not found. WebP plugin will not be available.")
        message(WARNING "To add libwebp as submodule, run:")
        message(WARNING "  git submodule add https://github.com/webmproject/libwebp.git lib/libwebp")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

if(LIBWEBP_FOUND)
    set(HAVE_LIBWEBP 1)
else()
    set(HAVE_LIBWEBP 0)
endif()

# Check if libtiff submodule exists
set(LIBTIFF_FOUND FALSE)
set(LIBTIFF_LIBRARIES "")
set(LIBTIFF_TARGET "")

# Skip building libtiff from source when APP_ONLY
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libtiff/CMakeLists.txt")
    message(STATUS "libtiff submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(LIBTIFF_BUILD_DIR "${CMAKE_BINARY_DIR}/libtiff_build")
    set(LIBTIFF_INSTALL_DIR "${CMAKE_BINARY_DIR}/libtiff_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows, libtiff outputs libtiff-6.dll (versioned)
        set(LIBTIFF_DLL_NAME "libtiff-6.dll")
        set(LIBTIFF_DLL_PATH "${LIBTIFF_INSTALL_DIR}/bin/${LIBTIFF_DLL_NAME}")
        
        # For MinGW, the import library is libtiff.dll.a
        if(MINGW)
            set(LIBTIFF_IMPLIB_NAME "libtiff.dll.a")
        else()
            set(LIBTIFF_IMPLIB_NAME "tiff.lib")
        endif()
        set(LIBTIFF_IMPLIB_PATH "${LIBTIFF_INSTALL_DIR}/lib/${LIBTIFF_IMPLIB_NAME}")
    else()
        # On Unix-like systems, shared library is in lib/
        set(LIBTIFF_LIB_NAME "libtiff${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBTIFF_LIB_PATH "${LIBTIFF_INSTALL_DIR}/lib/${LIBTIFF_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for libtiff
    # Only build shared library, not static
    set(LIBTIFF_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBTIFF_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -Dtiff-tools=OFF
        -Dtiff-tests=OFF
        -Dtiff-contrib=OFF
        -Dtiff-docs=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    
    # Pass zlib information to libtiff if we're building zlib from source
    if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
        if(WIN32)
            list(APPEND LIBTIFF_CMAKE_ARGS 
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_IMPLIB_PATH}
                -DCMAKE_PREFIX_PATH=${ZLIB_INSTALL_DIR}
            )
        else()
            list(APPEND LIBTIFF_CMAKE_ARGS 
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_LIB_PATH}
                -DCMAKE_PREFIX_PATH=${ZLIB_INSTALL_DIR}
            )
        endif()
    endif()
    
    # Pass jpeg information to libtiff if we're building jpeg from source
    if(LIBJPEG_TARGET AND TARGET ${LIBJPEG_TARGET})
        if(WIN32)
            list(APPEND LIBTIFF_CMAKE_ARGS 
                -DJPEG_ROOT=${LIBJPEG_INSTALL_DIR}
                -DJPEG_INCLUDE_DIR=${LIBJPEG_INCLUDE_DIRS}
                -DJPEG_LIBRARY=${LIBJPEG_IMPLIB_PATH}
                -DCMAKE_PREFIX_PATH=${LIBJPEG_INSTALL_DIR}
            )
        else()
            list(APPEND LIBTIFF_CMAKE_ARGS 
                -DJPEG_ROOT=${LIBJPEG_INSTALL_DIR}
                -DJPEG_INCLUDE_DIR=${LIBJPEG_INCLUDE_DIRS}
                -DJPEG_LIBRARY=${LIBJPEG_LIB_PATH}
                -DCMAKE_PREFIX_PATH=${LIBJPEG_INSTALL_DIR}
            )
        endif()
    endif()
    
    # Pass architecture-specific settings
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        list(APPEND LIBTIFF_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        # 32-bit
        list(APPEND LIBTIFF_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    # Collect dependencies for libtiff
    set(LIBTIFF_DEPENDS "")
    if(ZLIB_TARGET AND TARGET zlib_external)
        list(APPEND LIBTIFF_DEPENDS zlib_external)
    endif()
    if(LIBJPEG_TARGET AND TARGET libjpeg_external)
        list(APPEND LIBTIFF_DEPENDS libjpeg_external)
    endif()
    
    # Add ExternalProject to build libtiff
    if(LIBTIFF_DEPENDS)
        ExternalProject_Add(
            libtiff_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libtiff"
            BINARY_DIR "${LIBTIFF_BUILD_DIR}"
            INSTALL_DIR "${LIBTIFF_INSTALL_DIR}"
            CMAKE_ARGS ${LIBTIFF_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
            DEPENDS ${LIBTIFF_DEPENDS}
        )
    else()
        ExternalProject_Add(
            libtiff_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libtiff"
            BINARY_DIR "${LIBTIFF_BUILD_DIR}"
            INSTALL_DIR "${LIBTIFF_INSTALL_DIR}"
            CMAKE_ARGS ${LIBTIFF_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
        )
    endif()
    
    # Set include directory - use source directory for headers during configure
    # The installed headers will be available at build time
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libtiff/libtiff")
        set(LIBTIFF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libtiff/libtiff")
    else()
        # Fallback to source directory root
        set(LIBTIFF_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/libtiff")
    endif()
    
    include_directories(${LIBTIFF_INCLUDE_DIRS})
    
    # Create imported target for linking
    add_library(tiff SHARED IMPORTED GLOBAL)
    
    # Set properties
    if(WIN32)
        # On Windows, set the DLL location and import library
        set_target_properties(tiff PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBTIFF_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBTIFF_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBTIFF_IMPLIB_PATH}"
        )
        
        # Copy DLL to bin directory at build time
        add_custom_command(TARGET libtiff_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBTIFF_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libtiff DLL to bin directory"
        )
    else()
        # On Unix-like systems, set the shared library location
        set_target_properties(tiff PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBTIFF_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBTIFF_LIB_PATH}"
        )
        
        # Copy shared library to bin directory at build time
        add_custom_command(TARGET libtiff_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBTIFF_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libtiff shared library to bin directory"
        )
    endif()
    
    set(LIBTIFF_FOUND TRUE)
    set(LIBTIFF_TARGET tiff)
    
    message(STATUS "Building libtiff from source as shared library")
    message(STATUS "libtiff include dirs: ${LIBTIFF_INCLUDE_DIRS}")
    if(WIN32)
        message(STATUS "libtiff DLL path: ${LIBTIFF_DLL_PATH}")
        message(STATUS "libtiff import library path: ${LIBTIFF_IMPLIB_PATH}")
    else()
        message(STATUS "libtiff library path: ${LIBTIFF_LIB_PATH}")
    endif()
else()
    # Fallback to system libtiff via pkg-config
    message(STATUS "libtiff submodule not found, trying system libtiff")
    pkg_check_modules(LIBTIFF libtiff-4)
    
    if(NOT LIBTIFF_FOUND)
        pkg_check_modules(LIBTIFF libtiff)
    endif()
    
    if(LIBTIFF_FOUND)
        include_directories(${LIBTIFF_INCLUDE_DIRS})
        message(STATUS "Using system libtiff: ${LIBTIFF_INCLUDE_DIRS}")
    else()
        message(WARNING "libtiff not found. TIFF plugin will not be available.")
        message(WARNING "To add libtiff as submodule, run:")
        message(WARNING "  git submodule add https://gitlab.com/libtiff/libtiff.git lib/libtiff")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

if(LIBTIFF_FOUND)
    set(HAVE_LIBTIFF 1)
else()
    set(HAVE_LIBTIFF 0)
endif()

# Check if libde265 submodule exists (HEVC decoder for libheif HEIC support)
set(LIBDE265_FOUND FALSE)
set(LIBDE265_TARGET "")

# Skip building libde265 from source when APP_ONLY
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libde265/CMakeLists.txt")
    message(STATUS "libde265 submodule found, building from source for HEIC support")
    
    set(LIBDE265_BUILD_DIR "${CMAKE_BINARY_DIR}/libde265_build")
    set(LIBDE265_INSTALL_DIR "${CMAKE_BINARY_DIR}/libde265_install")
    
    if(WIN32)
        set(LIBDE265_DLL_NAME "libde265.dll")
        set(LIBDE265_DLL_PATH "${LIBDE265_INSTALL_DIR}/bin/${LIBDE265_DLL_NAME}")
        if(MINGW)
            set(LIBDE265_IMPLIB_NAME "libde265.dll.a")
        else()
            set(LIBDE265_IMPLIB_NAME "de265.lib")
        endif()
        set(LIBDE265_IMPLIB_PATH "${LIBDE265_INSTALL_DIR}/lib/${LIBDE265_IMPLIB_NAME}")
    else()
        set(LIBDE265_LIB_NAME "libde265${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBDE265_LIB_PATH "${LIBDE265_INSTALL_DIR}/lib/${LIBDE265_LIB_NAME}")
    endif()
    
    set(LIBDE265_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBDE265_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DENABLE_DECODER=ON
        -DENABLE_ENCODER=OFF
        -DENABLE_SDL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    
    ExternalProject_Add(
        libde265_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libde265"
        BINARY_DIR "${LIBDE265_BUILD_DIR}"
        INSTALL_DIR "${LIBDE265_INSTALL_DIR}"
        CMAKE_ARGS ${LIBDE265_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )
    
    set(LIBDE265_INCLUDE_DIRS "${LIBDE265_INSTALL_DIR}/include")
    set(LIBDE265_FOUND TRUE)
    set(LIBDE265_TARGET de265)
    
    message(STATUS "Building libde265 from source as shared library")
    message(STATUS "libde265 include dirs: ${LIBDE265_INCLUDE_DIRS}")
else()
    pkg_check_modules(LIBDE265 libde265)
    if(LIBDE265_FOUND)
        set(LIBDE265_INCLUDE_DIRS ${LIBDE265_INCLUDE_DIRS})
        message(STATUS "Using system libde265: ${LIBDE265_INCLUDE_DIRS}")
    endif()
endif()

# Check if libaom submodule exists (AV1 decoder for libheif AVIF support)
set(LIBAOM_FOUND FALSE)
set(LIBAOM_TARGET "")
set(HAVE_LIBAOM FALSE)

if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libaom/CMakeLists.txt")
    message(STATUS "libaom submodule found, building from source for AVIF support")

    set(LIBAOM_BUILD_DIR "${CMAKE_BINARY_DIR}/libaom_build")
    set(LIBAOM_INSTALL_DIR "${CMAKE_BINARY_DIR}/libaom_install")

    if(WIN32)
        if(MINGW)
            set(LIBAOM_DLL_NAME "libaom.dll")
        else()
            set(LIBAOM_DLL_NAME "aom.dll")
        endif()
        set(LIBAOM_DLL_PATH "${LIBAOM_INSTALL_DIR}/bin/${LIBAOM_DLL_NAME}")
        if(MINGW)
            set(LIBAOM_IMPLIB_NAME "libaom.dll.a")
        else()
            set(LIBAOM_IMPLIB_NAME "aom.lib")
        endif()
        set(LIBAOM_IMPLIB_PATH "${LIBAOM_INSTALL_DIR}/lib/${LIBAOM_IMPLIB_NAME}")
    else()
        set(LIBAOM_LIB_NAME "libaom${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBAOM_LIB_PATH "${LIBAOM_INSTALL_DIR}/lib/${LIBAOM_LIB_NAME}")
    endif()

    set(LIBAOM_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBAOM_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DENABLE_TESTS=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_TOOLS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )

    ExternalProject_Add(
        libaom_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libaom"
        BINARY_DIR "${LIBAOM_BUILD_DIR}"
        INSTALL_DIR "${LIBAOM_INSTALL_DIR}"
        CMAKE_ARGS ${LIBAOM_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )

    set(LIBAOM_FOUND TRUE)
    set(LIBAOM_TARGET aom)
    set(HAVE_LIBAOM TRUE)
    message(STATUS "Building libaom from source as shared library for AVIF support")
endif()

# Check if libheif submodule exists
set(LIBHEIF_FOUND FALSE)
set(LIBHEIF_LIBRARIES "")
set(LIBHEIF_TARGET "")

# Skip building libheif from source when APP_ONLY
if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libheif/CMakeLists.txt")
    message(STATUS "libheif submodule found, building from source")
    
    # Set up paths for ExternalProject
    set(LIBHEIF_BUILD_DIR "${CMAKE_BINARY_DIR}/libheif_build")
    set(LIBHEIF_INSTALL_DIR "${CMAKE_BINARY_DIR}/libheif_install")
    
    # Determine library name based on platform
    if(WIN32)
        # On Windows, MinGW uses lib prefix, MSVC does not
        if(MINGW)
            set(LIBHEIF_DLL_NAME "libheif.dll")
        else()
            set(LIBHEIF_DLL_NAME "heif.dll")
        endif()
        set(LIBHEIF_DLL_PATH "${LIBHEIF_INSTALL_DIR}/bin/${LIBHEIF_DLL_NAME}")
        
        if(MINGW)
            set(LIBHEIF_IMPLIB_NAME "libheif.dll.a")
        else()
            set(LIBHEIF_IMPLIB_NAME "heif.lib")
        endif()
        set(LIBHEIF_IMPLIB_PATH "${LIBHEIF_INSTALL_DIR}/lib/${LIBHEIF_IMPLIB_NAME}")
    else()
        set(LIBHEIF_LIB_NAME "libheif${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LIBHEIF_LIB_PATH "${LIBHEIF_INSTALL_DIR}/lib/${LIBHEIF_LIB_NAME}")
    endif()
    
    # Configure CMake arguments for libheif (minimal codecs: HEIC via libde265+x265, AVIF via libaom; smaller DLL)
    set(LIBHEIF_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBHEIF_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DWITH_EXAMPLES=OFF
        -DWITH_EXAMPLE_HEIF_THUMB=OFF
        -DWITH_EXAMPLE_HEIF_VIEW=OFF
        -DWITH_GDK_PIXBUF=OFF
        -DBUILD_DOCUMENTATION=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_PLUGIN_LOADING=OFF
        -DENABLE_EXPERIMENTAL_FEATURES=OFF
        -DWITH_X264=OFF
        -DWITH_DAV1D=OFF
        -DWITH_RAV1E=OFF
        -DWITH_SvtEnc=OFF
        -DWITH_JPEG_DECODER=OFF
        -DWITH_JPEG_ENCODER=OFF
        -DWITH_UNCOMPRESSED_CODEC=OFF
        -DWITH_KVAZAAR=OFF
        -DWITH_OpenJPEG_DECODER=OFF
        -DWITH_OpenJPEG_ENCODER=OFF
        -DWITH_FFMPEG_DECODER=OFF
        -DWITH_OpenH264_DECODER=OFF
        -DWITH_UVG266=OFF
        -DWITH_VVDEC=OFF
        -DWITH_VVENC=OFF
        -DWITH_REDUCED_VISIBILITY=ON
        -DWITH_HEADER_COMPRESSION=OFF
        -DWITH_FUZZERS=OFF
    )
    
    # Pass zlib to libheif if building from source
    set(LIBHEIF_CMAKE_PREFIX_PATH "")
    if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
        list(APPEND LIBHEIF_CMAKE_PREFIX_PATH ${ZLIB_INSTALL_DIR})
        if(WIN32)
            list(APPEND LIBHEIF_CMAKE_ARGS
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_IMPLIB_PATH}
            )
        else()
            list(APPEND LIBHEIF_CMAKE_ARGS
                -DZLIB_ROOT=${ZLIB_INSTALL_DIR}
                -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL_DIR}/include
                -DZLIB_LIBRARY=${ZLIB_LIB_PATH}
            )
        endif()
    endif()
    
    # Pass libde265 to libheif for HEIC support
    if(LIBDE265_FOUND AND LIBDE265_TARGET)
        list(APPEND LIBHEIF_CMAKE_PREFIX_PATH ${LIBDE265_INSTALL_DIR})
        list(APPEND LIBHEIF_CMAKE_ARGS
            -DWITH_LIBDE265=ON
            -DWITH_LIBDE265_PLUGIN=OFF
        )
    endif()

    # Pass libaom to libheif for AVIF support (decode + encode)
    if(LIBAOM_FOUND AND LIBAOM_TARGET AND TARGET libaom_external)
        list(APPEND LIBHEIF_CMAKE_PREFIX_PATH ${LIBAOM_INSTALL_DIR})
        list(APPEND LIBHEIF_CMAKE_ARGS
            -DWITH_AOM_DECODER=ON
            -DWITH_AOM_ENCODER=ON
            -DWITH_AOM_DECODER_PLUGIN=OFF
        )
    endif()
    # So libheif can find x265 (HEIC encode) and other MSYS2 packages when not built in-tree
    if(DEFINED ENV{MINGW_PREFIX} AND NOT "$ENV{MINGW_PREFIX}" STREQUAL "")
        list(APPEND LIBHEIF_CMAKE_PREFIX_PATH "$ENV{MINGW_PREFIX}")
    endif()

    if(LIBHEIF_CMAKE_PREFIX_PATH)
        list(APPEND LIBHEIF_CMAKE_ARGS -DCMAKE_PREFIX_PATH="${LIBHEIF_CMAKE_PREFIX_PATH}")
    endif()

    # Collect dependencies for libheif
    set(LIBHEIF_DEPENDS "")
    if(ZLIB_TARGET AND TARGET zlib_external)
        list(APPEND LIBHEIF_DEPENDS zlib_external)
    endif()
    if(LIBDE265_TARGET AND TARGET libde265_external)
        list(APPEND LIBHEIF_DEPENDS libde265_external)
    endif()
    if(LIBAOM_TARGET AND TARGET libaom_external)
        list(APPEND LIBHEIF_DEPENDS libaom_external)
    endif()
    
    # Add ExternalProject to build libheif
    if(LIBHEIF_DEPENDS)
        ExternalProject_Add(
            libheif_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libheif"
            BINARY_DIR "${LIBHEIF_BUILD_DIR}"
            INSTALL_DIR "${LIBHEIF_INSTALL_DIR}"
            CMAKE_ARGS ${LIBHEIF_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
            DEPENDS ${LIBHEIF_DEPENDS}
        )
    else()
        ExternalProject_Add(
            libheif_external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libheif"
            BINARY_DIR "${LIBHEIF_BUILD_DIR}"
            INSTALL_DIR "${LIBHEIF_INSTALL_DIR}"
            CMAKE_ARGS ${LIBHEIF_CMAKE_ARGS}
            BUILD_ALWAYS FALSE
        )
    endif()
    
    # Generate heif_version.h (required by libheif headers, normally built by libheif)
    set(LIBHEIF_VERSION_MAJOR 1)
    set(LIBHEIF_VERSION_MINOR 21)
    set(LIBHEIF_VERSION_PATCH 2)
    set(LIBHEIF_INCLUDE_GEN_DIR "${CMAKE_BINARY_DIR}/libheif_include")
    file(MAKE_DIRECTORY "${LIBHEIF_INCLUDE_GEN_DIR}/libheif")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/heif_version.h.in"
        "${LIBHEIF_INCLUDE_GEN_DIR}/libheif/heif_version.h"
        @ONLY
    )
    # Include: source api (libheif/heif.h etc) + generated (libheif/heif_version.h)
    set(LIBHEIF_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/lib/libheif/libheif/api"
        "${LIBHEIF_INCLUDE_GEN_DIR}"
    )
    
    include_directories(${LIBHEIF_INCLUDE_DIRS})
    
    # Create imported target for linking
    add_library(heif SHARED IMPORTED GLOBAL)
    
    # Set properties
    if(WIN32)
        set_target_properties(heif PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBHEIF_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBHEIF_DLL_PATH}"
            IMPORTED_IMPLIB "${LIBHEIF_IMPLIB_PATH}"
        )
        
        add_custom_command(TARGET libheif_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBHEIF_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libheif DLL to bin directory"
        )
        if(LIBDE265_FOUND AND DEFINED LIBDE265_DLL_PATH)
            add_custom_command(TARGET libheif_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIBDE265_DLL_PATH}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libde265 DLL to bin directory (HEIC runtime dependency)"
            )
        endif()
        if(LIBAOM_FOUND AND DEFINED LIBAOM_DLL_PATH)
            add_custom_command(TARGET libheif_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIBAOM_DLL_PATH}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libaom DLL to bin directory (AVIF runtime dependency)"
            )
        endif()
    else()
        set_target_properties(heif PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBHEIF_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LIBHEIF_LIB_PATH}"
        )
        
        add_custom_command(TARGET libheif_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LIBHEIF_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying libheif shared library to bin directory"
        )
        if(LIBDE265_FOUND AND DEFINED LIBDE265_LIB_PATH)
            add_custom_command(TARGET libheif_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIBDE265_LIB_PATH}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libde265 shared library to bin directory (HEIC runtime dependency)"
            )
        endif()
        if(LIBAOM_FOUND AND DEFINED LIBAOM_LIB_PATH)
            add_custom_command(TARGET libheif_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LIBAOM_LIB_PATH}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libaom shared library to bin directory (AVIF runtime dependency)"
            )
        endif()
    endif()

    set(LIBHEIF_FOUND TRUE)
    set(LIBHEIF_TARGET heif)
    
    message(STATUS "Building libheif from source as shared library")
    message(STATUS "libheif include dirs: ${LIBHEIF_INCLUDE_DIRS}")
else()
    # Fallback to system libheif via pkg-config
    message(STATUS "libheif submodule not found, trying system libheif")
    pkg_check_modules(LIBHEIF libheif)
    
    if(LIBHEIF_FOUND)
        include_directories(${LIBHEIF_INCLUDE_DIRS})
        message(STATUS "Using system libheif: ${LIBHEIF_INCLUDE_DIRS}")
    else()
        message(WARNING "libheif not found. HEIF/AVIF plugin will not be available.")
        message(WARNING "To add libheif as submodule, run:")
        message(WARNING "  git submodule add https://github.com/strukturag/libheif.git lib/libheif")
        message(WARNING "  git submodule update --init --recursive")
    endif()
endif()

if(LIBHEIF_FOUND)
    set(HAVE_LIBHEIF 1)
else()
    set(HAVE_LIBHEIF 0)
endif()

# Check if OpenEXR submodule exists (skip when APP_ONLY: use system libs)
set(OPENEXR_FOUND FALSE)
set(OPENEXR_LIBRARIES "")
set(OPENEXR_TARGET "")

if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/openexr/CMakeLists.txt")
    message(STATUS "OpenEXR submodule found, building from source")

    # Read OpenEXR version for library suffix (e.g. 4_0)
    set(OPENEXR_VERSION_H "${CMAKE_SOURCE_DIR}/lib/openexr/src/lib/OpenEXRCore/openexr_version.h")
    set(OPENEXR_VERSION_API "4_0")
    set(OPENEXR_VERSION_MAJOR "")
    set(OPENEXR_VERSION_MINOR "")
    if(EXISTS "${OPENEXR_VERSION_H}")
        file(STRINGS "${OPENEXR_VERSION_H}" OPENEXR_MAJOR_LINES REGEX "#define OPENEXR_VERSION_MAJOR ")
        file(STRINGS "${OPENEXR_VERSION_H}" OPENEXR_MINOR_LINES REGEX "#define OPENEXR_VERSION_MINOR ")
        if(OPENEXR_MAJOR_LINES)
            list(GET OPENEXR_MAJOR_LINES 0 OPENEXR_MAJOR_LINE)
            string(REGEX REPLACE ".*OPENEXR_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" OPENEXR_VERSION_MAJOR "${OPENEXR_MAJOR_LINE}")
        endif()
        if(OPENEXR_MINOR_LINES)
            list(GET OPENEXR_MINOR_LINES 0 OPENEXR_MINOR_LINE)
            string(REGEX REPLACE ".*OPENEXR_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" OPENEXR_VERSION_MINOR "${OPENEXR_MINOR_LINE}")
        endif()
        if(DEFINED OPENEXR_VERSION_MAJOR AND DEFINED OPENEXR_VERSION_MINOR AND NOT "${OPENEXR_VERSION_MAJOR}" STREQUAL "" AND NOT "${OPENEXR_VERSION_MINOR}" STREQUAL "")
            set(OPENEXR_VERSION_API "${OPENEXR_VERSION_MAJOR}_${OPENEXR_VERSION_MINOR}")
        endif()
    endif()

    set(OPENEXR_BUILD_DIR "${CMAKE_BINARY_DIR}/openexr_build")
    set(OPENEXR_INSTALL_DIR "${CMAKE_BINARY_DIR}/openexr_install")

    # MinGW: bundled OpenEXR sets CMAKE_DEBUG_POSTFIX to "_d" (OpenEXRSetup.cmake).
    # Installed DLLs/import libs use libOpenEXR-4_0[_d].dll / .dll.a (not OpenEXR-4_0.dll).
    set(OPENEXR_MINGW_LIB_POSTFIX "")
    # Imath API segment in libImath-<api> (bundled via OpenEXR FetchContent; bump when upgrading).
    set(IMATH_VERSION_API "3_2")
    if(MINGW)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _openexr_rl_bt)
        if(_openexr_rl_bt STREQUAL "DEBUG")
            set(OPENEXR_MINGW_LIB_POSTFIX "_d")
        endif()
        unset(_openexr_rl_bt)
    endif()

    if(WIN32)
        set(OPENEXR_DLL_PREFIX "${OPENEXR_INSTALL_DIR}/bin")
        set(OPENEXR_LIB_PREFIX "${OPENEXR_INSTALL_DIR}/lib")
    else()
        set(OPENEXR_LIB_PREFIX "${OPENEXR_INSTALL_DIR}/lib")
    endif()

    set(OPENEXR_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${OPENEXR_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON
        -DOPENEXR_BUILD_LIBS=ON
        -DOPENEXR_BUILD_TOOLS=OFF
        -DOPENEXR_BUILD_EXAMPLES=OFF
        -DOPENEXR_INSTALL=ON
        -DOPENEXR_INSTALL_TOOLS=OFF
        -DOPENEXR_INSTALL_PKG_CONFIG=OFF
        -DOPENEXR_FORCE_INTERNAL_IMATH=ON
        -DOPENEXR_FORCE_INTERNAL_DEFLATE=ON
        -DOPENEXR_FORCE_INTERNAL_OPENJPH=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )

    # MinGW + shared OpenEXR: explicit template instantiations (e.g. TypedAttribute::value)
    # must end up in the DLL export table. Upstream leaves IMF_EXPORT_TEMPLATE_INSTANCE
    # empty on MinGW; exporting all symbols avoids undefined references when linking
    # website/examples without patching vendored sources.
    # OpenEXRCoreTest links internal C APIs (internal_huf_*, exrcore_ensure_*) that are
    # not exported from OpenEXRCore.dll on PE/MinGW; skip upstream library tests only.
    if(MINGW)
        list(APPEND OPENEXR_CMAKE_ARGS
            -DOPENEXR_USE_DEFAULT_VISIBILITY=ON
            "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--export-all-symbols"
            -DOPENEXR_TEST_LIBRARIES=OFF
        )
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND OPENEXR_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    else()
        list(APPEND OPENEXR_CMAKE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()

    # On Windows, Git stores the OpenJPH "openjph" symlink as a text file ("common"),
    # so <openjph/ojph_params.h> fails and the compiler picks up system OpenJPH (older API).
    # Create a real openjph directory and copy vendored headers so the build uses bundled OpenJPH.
    set(OPENEXR_OPENJPH_CORE "${CMAKE_SOURCE_DIR}/lib/openexr/external/OpenJPH/src/core")
    set(OPENEXR_OPENJPH_OPENJPH "${OPENEXR_OPENJPH_CORE}/openjph")
    set(OPENEXR_OPENJPH_COMMON "${OPENEXR_OPENJPH_CORE}/common")
    if(NOT IS_DIRECTORY "${OPENEXR_OPENJPH_OPENJPH}" AND EXISTS "${OPENEXR_OPENJPH_COMMON}")
        message(STATUS "OpenEXR: creating openjph include dir for Windows (Git symlink fix)")
        if(EXISTS "${OPENEXR_OPENJPH_OPENJPH}")
            file(REMOVE "${OPENEXR_OPENJPH_OPENJPH}")
        endif()
        file(MAKE_DIRECTORY "${OPENEXR_OPENJPH_OPENJPH}")
        file(GLOB OPENEXR_OPENJPH_HEADERS "${OPENEXR_OPENJPH_COMMON}/*.h")
        foreach(H ${OPENEXR_OPENJPH_HEADERS})
            file(COPY "${H}" DESTINATION "${OPENEXR_OPENJPH_OPENJPH}")
        endforeach()
    endif()

    # Stub dirs so INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets exists before
    # openexr_external install populates headers (CMake 3.24+ validates these paths).
    file(MAKE_DIRECTORY "${OPENEXR_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${OPENEXR_INSTALL_DIR}/include/OpenEXR")
    file(MAKE_DIRECTORY "${OPENEXR_INSTALL_DIR}/include/Imath")

    # Makefile generators need explicit outputs or they try to add a rule for IMPORTED_IMPLIB paths.
    set(_openexr_ep_extra "")
    if(MINGW)
        set(_openexr_ep_extra
            BUILD_BYPRODUCTS
                "${OPENEXR_INSTALL_DIR}/lib/libIex-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a"
                "${OPENEXR_INSTALL_DIR}/lib/libIlmThread-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a"
                "${OPENEXR_INSTALL_DIR}/lib/libOpenEXRCore-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a"
                "${OPENEXR_INSTALL_DIR}/lib/libOpenEXR-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a"
                "${OPENEXR_INSTALL_DIR}/lib/libImath-${IMATH_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a"
        )
    endif()

    ExternalProject_Add(
        openexr_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/openexr"
        BINARY_DIR "${OPENEXR_BUILD_DIR}"
        INSTALL_DIR "${OPENEXR_INSTALL_DIR}"
        CMAKE_ARGS ${OPENEXR_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
        ${_openexr_ep_extra}
    )
    unset(_openexr_ep_extra)

    # include/, include/OpenEXR/, and include/Imath/ needed for openexr.h and its transitive includes
    set(OPENEXR_INCLUDE_DIRS
        "${OPENEXR_INSTALL_DIR}/include"
        "${OPENEXR_INSTALL_DIR}/include/OpenEXR"
        "${OPENEXR_INSTALL_DIR}/include/Imath")
    include_directories(${OPENEXR_INCLUDE_DIRS})

    # Main OpenEXR library (Imf) - primary target for linking
    set(OPENEXR_MAIN_LIB_NAME "OpenEXR-${OPENEXR_VERSION_API}")
    set(OPENEXR_MAIN_IMPLIB_NAME "OpenEXR-${OPENEXR_VERSION_API}.lib")
    if(MINGW)
        set(OPENEXR_MAIN_IMPLIB_NAME "libOpenEXR-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a")
    endif()

    add_library(OpenEXR::OpenEXR SHARED IMPORTED GLOBAL)
    set_target_properties(OpenEXR::OpenEXR PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OPENEXR_INCLUDE_DIRS}"
    )
    if(WIN32)
        if(MINGW)
            set_target_properties(OpenEXR::OpenEXR PROPERTIES
                IMPORTED_LOCATION "${OPENEXR_DLL_PREFIX}/libOpenEXR-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}${CMAKE_SHARED_LIBRARY_SUFFIX}"
                IMPORTED_IMPLIB "${OPENEXR_LIB_PREFIX}/${OPENEXR_MAIN_IMPLIB_NAME}"
            )
        else()
            set_target_properties(OpenEXR::OpenEXR PROPERTIES
                IMPORTED_LOCATION "${OPENEXR_DLL_PREFIX}/OpenEXR-${OPENEXR_VERSION_API}${CMAKE_SHARED_LIBRARY_SUFFIX}"
                IMPORTED_IMPLIB "${OPENEXR_LIB_PREFIX}/${OPENEXR_MAIN_IMPLIB_NAME}"
            )
        endif()
    else()
        set_target_properties(OpenEXR::OpenEXR PROPERTIES
            IMPORTED_LOCATION "${OPENEXR_LIB_PREFIX}/libOpenEXR-${OPENEXR_VERSION_API}${CMAKE_SHARED_LIBRARY_SUFFIX}"
        )
    endif()

    # OpenEXRCore (C API exr_*) - required for EXR plugin
    set(OPENEXR_CORE_LIB_NAME "OpenEXRCore-${OPENEXR_VERSION_API}")
    set(OPENEXR_CORE_IMPLIB_NAME "OpenEXRCore-${OPENEXR_VERSION_API}.lib")
    if(MINGW)
        set(OPENEXR_CORE_IMPLIB_NAME "libOpenEXRCore-${OPENEXR_VERSION_API}${OPENEXR_MINGW_LIB_POSTFIX}.dll.a")
    endif()
    add_library(OpenEXR::OpenEXRCore SHARED IMPORTED GLOBAL)
    set_target_properties(OpenEXR::OpenEXRCore PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OPENEXR_INCLUDE_DIRS}"
    )
    if(WIN32)
        set_target_properties(OpenEXR::OpenEXRCore PROPERTIES
            IMPORTED_LOCATION "${OPENEXR_DLL_PREFIX}/lib${OPENEXR_CORE_LIB_NAME}${OPENEXR_MINGW_LIB_POSTFIX}${CMAKE_SHARED_LIBRARY_SUFFIX}"
            IMPORTED_IMPLIB "${OPENEXR_LIB_PREFIX}/${OPENEXR_CORE_IMPLIB_NAME}"
        )
    else()
        set_target_properties(OpenEXR::OpenEXRCore PROPERTIES
            IMPORTED_LOCATION "${OPENEXR_LIB_PREFIX}/lib${OPENEXR_CORE_LIB_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}"
        )
    endif()

    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/CopyOpenEXRDLLs.cmake"
        "${CMAKE_BINARY_DIR}/cmake/CopyOpenEXRDLLs.cmake"
        @ONLY
    )
    add_custom_command(TARGET openexr_external POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/cmake/CopyOpenEXRDLLs.cmake"
        COMMENT "Copying OpenEXR (and Imath) DLLs/shared libs to bin directory"
    )

    set(OPENEXR_FOUND TRUE)
    set(OPENEXR_TARGET OpenEXR::OpenEXR)
    message(STATUS "Building OpenEXR from source as shared library")
    message(STATUS "OpenEXR include dirs: ${OPENEXR_INCLUDE_DIRS}")
else()
    # Fallback to system OpenEXR via find_package
    find_package(OpenEXR QUIET)
    if(OpenEXR_FOUND)
        set(OPENEXR_FOUND TRUE)
        set(OPENEXR_TARGET OpenEXR::OpenEXR)
        message(STATUS "Using system OpenEXR")
    else()
        message(STATUS "OpenEXR not found. EXR/HDR plugin may use alternative or no OpenEXR.")
    endif()
endif()

if(OPENEXR_FOUND)
    set(HAVE_OPENEXR 1)
else()
    set(HAVE_OPENEXR 0)
endif()

# ---------------------------------------------------------------------------
# Little CMS (lcms2)
# ---------------------------------------------------------------------------
set(LCMS2_FOUND FALSE)
set(LCMS2_TARGET "")

if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/lcms/CMakeLists.txt")
    message(STATUS "Little CMS (lcms) submodule found, building from source as shared library")

    set(LCMS2_BUILD_DIR "${CMAKE_BINARY_DIR}/lcms2_build")
    set(LCMS2_INSTALL_DIR "${CMAKE_BINARY_DIR}/lcms2_install")

    if(WIN32)
        set(LCMS2_DLL_NAME "${CMAKE_SHARED_LIBRARY_PREFIX}lcms2${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LCMS2_DLL_PATH "${LCMS2_INSTALL_DIR}/bin/${LCMS2_DLL_NAME}")
        if(MINGW)
            set(LCMS2_IMPLIB_NAME "liblcms2.dll.a")
        else()
            set(LCMS2_IMPLIB_NAME "lcms2.lib")
        endif()
        set(LCMS2_IMPLIB_PATH "${LCMS2_INSTALL_DIR}/lib/${LCMS2_IMPLIB_NAME}")
    else()
        set(LCMS2_LIB_NAME "${CMAKE_SHARED_LIBRARY_PREFIX}lcms2${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(LCMS2_LIB_PATH "${LCMS2_INSTALL_DIR}/lib/${LCMS2_LIB_NAME}")
    endif()

    set(LCMS2_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LCMS2_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DLCMS2_BUILD_SHARED=ON
        -DLCMS2_BUILD_STATIC=OFF
        -DLCMS2_BUILD_TOOLS=OFF
        -DLCMS2_BUILD_TESTS=OFF
        -DLCMS2_BUILD_JPGICC=OFF
        -DLCMS2_BUILD_TIFICC=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )

    ExternalProject_Add(
        lcms2_external
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/lcms"
        BINARY_DIR "${LCMS2_BUILD_DIR}"
        INSTALL_DIR "${LCMS2_INSTALL_DIR}"
        CMAKE_ARGS ${LCMS2_CMAKE_ARGS}
        BUILD_ALWAYS FALSE
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/lcms/include")
        set(LCMS2_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/lcms/include")
    else()
        set(LCMS2_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/lib/lcms")
    endif()

    include_directories(${LCMS2_INCLUDE_DIRS})

    add_library(lcms2 SHARED IMPORTED GLOBAL)

    if(WIN32)
        set_target_properties(lcms2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LCMS2_DLL_PATH}"
            IMPORTED_IMPLIB "${LCMS2_IMPLIB_PATH}"
        )
        add_custom_command(TARGET lcms2_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LCMS2_DLL_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying Little CMS DLL to bin directory"
        )
    else()
        set_target_properties(lcms2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${LCMS2_LIB_PATH}"
        )
        add_custom_command(TARGET lcms2_external POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LCMS2_LIB_PATH}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying Little CMS shared library to bin directory"
        )
    endif()

    set(LCMS2_FOUND TRUE)
    set(LCMS2_TARGET lcms2)
    message(STATUS "Little CMS include dirs: ${LCMS2_INCLUDE_DIRS}")
else()
    pkg_check_modules(LCMS2 lcms2)
    if(LCMS2_FOUND)
        include_directories(${LCMS2_INCLUDE_DIRS})
        message(STATUS "Using system Little CMS: ${LCMS2_INCLUDE_DIRS}")
    else()
        message(STATUS "Little CMS not found. Color management (lcms2) will not be available.")
    endif()
endif()

if(LCMS2_FOUND)
    set(HAVE_LCMS2 1)
else()
    set(HAVE_LCMS2 0)
endif()

# ---------------------------------------------------------------------------
# JPEG XL (libjxl) — shared library via ExternalProject (requires C++ toolchain)
# Uses Little CMS: bundled lcms2 install dir, or system lcms2 (CMAKE_PREFIX_PATH).
# ---------------------------------------------------------------------------
set(LIBJXL_FOUND FALSE)
set(LIBJXL_TARGET "")

if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libjxl/CMakeLists.txt" AND HAVE_LCMS2)
    set(_libjxl_lcms_prefix "")
    if(DEFINED LCMS2_INSTALL_DIR)
        set(_libjxl_lcms_prefix "${LCMS2_INSTALL_DIR}")
    elseif(LCMS2_FOUND)
        find_path(_rl_lcms2_include_dir NAMES lcms2.h PATHS ${LCMS2_INCLUDE_DIRS} NO_DEFAULT_PATH)
        if(NOT _rl_lcms2_include_dir)
            find_path(_rl_lcms2_include_dir NAMES lcms2.h)
        endif()
        if(_rl_lcms2_include_dir)
            get_filename_component(_libjxl_lcms_prefix "${_rl_lcms2_include_dir}" DIRECTORY)
        endif()
    endif()
    if(NOT _libjxl_lcms_prefix)
        message(WARNING "libjxl: could not determine lcms2 install prefix. Skipping libjxl build.")
    else()
        message(STATUS "libjxl submodule found, building from source as shared library")

        set(LIBJXL_BUILD_DIR "${CMAKE_BINARY_DIR}/libjxl_build")
        set(LIBJXL_INSTALL_DIR "${CMAKE_BINARY_DIR}/libjxl_install")
        # Stub install tree so INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets exists at configure time
        file(MAKE_DIRECTORY "${LIBJXL_INSTALL_DIR}/include/jxl")

        # Smaller libjxl: avoid compiling every Highway CPU variant (upstream notes each adds binary size)
        set(_libjxl_bt ${CMAKE_BUILD_TYPE})
        if(RASTERLAB_LIBJXL_SMALL AND NOT _libjxl_bt STREQUAL "Debug")
            set(_libjxl_bt "MinSizeRel")
        endif()

        if(WIN32)
            set(LIBJXL_DLL_PATH "${LIBJXL_INSTALL_DIR}/bin/libjxl.dll")
            set(LIBJXL_CMS_DLL_PATH "${LIBJXL_INSTALL_DIR}/bin/libjxl_cms.dll")
            set(LIBJXL_THREADS_DLL_PATH "${LIBJXL_INSTALL_DIR}/bin/libjxl_threads.dll")
            if(MINGW)
                set(LIBJXL_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl.dll.a")
                set(LIBJXL_CMS_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl_cms.dll.a")
                set(LIBJXL_THREADS_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl_threads.dll.a")
            else()
                set(LIBJXL_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/jxl.lib")
                set(LIBJXL_CMS_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/jxl_cms.lib")
                set(LIBJXL_THREADS_IMPLIB_PATH "${LIBJXL_INSTALL_DIR}/lib/jxl_threads.lib")
            endif()
        else()
            set(LIBJXL_LIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(LIBJXL_CMS_LIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl_cms${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(LIBJXL_THREADS_LIB_PATH "${LIBJXL_INSTALL_DIR}/lib/libjxl_threads${CMAKE_SHARED_LIBRARY_SUFFIX}")
        endif()

        set(LIBJXL_CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${LIBJXL_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=${_libjxl_bt}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DBUILD_SHARED_LIBS=ON
            -DBUILD_TESTING=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DJPEGXL_ENABLE_SKCMS=OFF
            -DJPEGXL_FORCE_SYSTEM_LCMS2=ON
            -DCMAKE_PREFIX_PATH=${_libjxl_lcms_prefix}
            -DJPEGXL_ENABLE_TOOLS=OFF
            -DJPEGXL_ENABLE_DEVTOOLS=OFF
            -DJPEGXL_ENABLE_BENCHMARK=OFF
            -DJPEGXL_ENABLE_EXAMPLES=OFF
            -DJPEGXL_ENABLE_DOXYGEN=OFF
            -DJPEGXL_ENABLE_MANPAGES=OFF
            -DJPEGXL_ENABLE_JNI=OFF
            -DJPEGXL_ENABLE_SJPEG=OFF
            -DJPEGXL_ENABLE_OPENEXR=OFF
            -DJPEGXL_ENABLE_FUZZERS=OFF
            -DJPEGXL_ENABLE_TCMALLOC=OFF
        )

        # 64-bit x86: drop AVX-512, ARM, POWER, RISC-V, WASM, IBM Z, etc. (keep SCALAR + SSE2/4 + AVX2)
        if(RASTERLAB_LIBJXL_SMALL AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
            foreach(_libjxl_hwy IN ITEMS
                    AVX3 AVX3_DL AVX3_SPR AVX3_ZEN4
                    NEON NEON_BF16 NEON_WITHOUT_AES
                    PPC8 PPC9 PPC10
                    RVV
                    SVE SVE_256 SVE2 SVE2_128
                    WASM WASM_EMU256
                    Z14 Z15)
                list(APPEND LIBJXL_CMAKE_ARGS "-DJPEGXL_ENABLE_HWY_${_libjxl_hwy}=OFF")
            endforeach()
            message(STATUS "libjxl: RASTERLAB_LIBJXL_SMALL=ON — reduced Highway targets for typical x86_64 desktop")
        elseif(RASTERLAB_LIBJXL_SMALL)
            message(STATUS "libjxl: RASTERLAB_LIBJXL_SMALL=ON — Highway target trimming skipped (not x86_64)")
        endif()

        # Strip symbols from the DLL (large savings vs unstripped Release)
        if(RASTERLAB_LIBJXL_SMALL AND MINGW AND NOT _libjxl_bt STREQUAL "Debug")
            list(APPEND LIBJXL_CMAKE_ARGS "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-s")
        endif()

        set(LIBJXL_DEPENDS "")
        if(TARGET lcms2_external)
            list(APPEND LIBJXL_DEPENDS lcms2_external)
        endif()

        if(LIBJXL_DEPENDS)
            ExternalProject_Add(
                libjxl_external
                SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libjxl"
                BINARY_DIR "${LIBJXL_BUILD_DIR}"
                INSTALL_DIR "${LIBJXL_INSTALL_DIR}"
                CMAKE_ARGS ${LIBJXL_CMAKE_ARGS}
                BUILD_ALWAYS FALSE
                DEPENDS ${LIBJXL_DEPENDS}
            )
        else()
            ExternalProject_Add(
                libjxl_external
                SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/libjxl"
                BINARY_DIR "${LIBJXL_BUILD_DIR}"
                INSTALL_DIR "${LIBJXL_INSTALL_DIR}"
                CMAKE_ARGS ${LIBJXL_CMAKE_ARGS}
                BUILD_ALWAYS FALSE
            )
        endif()

        set(LIBJXL_INCLUDE_DIRS "${LIBJXL_INSTALL_DIR}/include")

        add_library(jpegxl SHARED IMPORTED GLOBAL)
        add_library(jpegxl_cms SHARED IMPORTED GLOBAL)
        add_library(jpegxl_threads SHARED IMPORTED GLOBAL)

        if(WIN32)
            set_target_properties(jpegxl PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_DLL_PATH}"
                IMPORTED_IMPLIB "${LIBJXL_IMPLIB_PATH}"
            )
            set_target_properties(jpegxl_cms PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_CMS_DLL_PATH}"
                IMPORTED_IMPLIB "${LIBJXL_CMS_IMPLIB_PATH}"
            )
            set_target_properties(jpegxl_threads PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_THREADS_DLL_PATH}"
                IMPORTED_IMPLIB "${LIBJXL_THREADS_IMPLIB_PATH}"
            )
            add_custom_command(TARGET libjxl_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_DLL_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_CMS_DLL_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_THREADS_DLL_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libjxl DLLs to bin directory"
            )
        else()
            set_target_properties(jpegxl PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_LIB_PATH}"
            )
            set_target_properties(jpegxl_cms PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_CMS_LIB_PATH}"
            )
            set_target_properties(jpegxl_threads PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBJXL_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${LIBJXL_THREADS_LIB_PATH}"
            )
            add_custom_command(TARGET libjxl_external POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_LIB_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_CMS_LIB_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBJXL_THREADS_LIB_PATH}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                COMMENT "Copying libjxl shared libraries to bin directory"
            )
        endif()

        set(LIBJXL_FOUND TRUE)
        set(LIBJXL_TARGET jpegxl)
        message(STATUS "libjxl install dir: ${LIBJXL_INSTALL_DIR}")
    endif()
else()
    if(NOT RASTERLAB_APP_ONLY AND EXISTS "${CMAKE_SOURCE_DIR}/lib/libjxl/CMakeLists.txt" AND NOT HAVE_LCMS2)
        message(STATUS "libjxl: lcms2 not available; skipping bundled libjxl (JPEG XL needs lcms2 when skcms is disabled)")
    endif()
    if(RASTERLAB_APP_ONLY OR NOT EXISTS "${CMAKE_SOURCE_DIR}/lib/libjxl/CMakeLists.txt")
        message(STATUS "libjxl submodule not built from source, trying pkg-config libjxl")
        pkg_check_modules(LIBJXL libjxl)
        if(LIBJXL_FOUND)
            include_directories(${LIBJXL_INCLUDE_DIRS})
        endif()
    endif()
endif()

if(LIBJXL_FOUND)
    set(HAVE_LIBJXL 1)
else()
    set(HAVE_LIBJXL 0)
endif()

# Find GTK3, GdkPixbuf, libxml2, and LZ4 using pkg-config
# NOTE: This is done AFTER zlib/libpng configuration to ensure our libpng headers
# are set up before GTK3's pkg-config adds system libpng headers to CFLAGS
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

# ---------------------------------------------------------------------------
# GLFW: add as static library
# ---------------------------------------------------------------------------
set(GLFW_FOUND FALSE)
set(GLFW_TARGET "")
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/glfw/CMakeLists.txt")
    message(STATUS "GLFW submodule found, building as static library")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "Generate GLFW install target" FORCE)
    set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "Build GLFW as static library" FORCE)
    add_subdirectory(lib/glfw)
    set(GLFW_FOUND TRUE)
    set(GLFW_TARGET glfw)
    message(STATUS "GLFW configured as static library")
else()
    message(STATUS "GLFW not found in lib/glfw")
endif()

# Add subdirectories
add_subdirectory(src)

# Optional: build test suite (requires Unity submodule in lib/unity)
option(BUILD_TESTING "Build the RLI unit test suite" OFF)
if(BUILD_TESTING AND EXISTS "${CMAKE_SOURCE_DIR}/lib/unity/src/unity.c")
    enable_testing()
    add_subdirectory(tests)
    message(STATUS "BUILD_TESTING=ON: RLI test suite enabled")
endif()

# Print configuration summary
message(STATUS "GTK3 found: ${GTK3_LIBRARIES}")
message(STATUS "GTK3 include dirs: ${GTK3_INCLUDE_DIRS}")
message(STATUS "GTK3 library dirs: ${GTK3_LIBRARY_DIRS}")
message(STATUS "GTK3 CFLAGS: ${GTK3_CFLAGS}")
message(STATUS "GTK3 LDFLAGS: ${GTK3_LDFLAGS}")
message(STATUS "GdkPixbuf found: Yes")
