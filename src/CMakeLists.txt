# Find glib-compile-resources for gresources compilation
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

# Parse gresources.xml to extract all file dependencies
set(GRESOURCE_XML ${CMAKE_SOURCE_DIR}/resources/gresources.xml)
file(READ ${GRESOURCE_XML} GRESOURCE_XML_CONTENT)
set(GRESOURCE_DEPENDENCIES ${GRESOURCE_XML})

# Extract all file paths from gresources.xml
string(REGEX MATCHALL "<file[^>]*>([^<]+)</file>" FILE_MATCHES "${GRESOURCE_XML_CONTENT}")
foreach(MATCH ${FILE_MATCHES})
    string(REGEX REPLACE "<file[^>]*>([^<]+)</file>" "\\1" FILE_PATH "${MATCH}")
    # Remove any preprocessing attributes (e.g., "preprocess="to-pixdata"")
    string(REGEX REPLACE "^[^>]*>" "" FILE_PATH "${FILE_PATH}")
    string(REGEX REPLACE "</file>$" "" FILE_PATH "${FILE_PATH}")
    # Trim whitespace
    string(STRIP "${FILE_PATH}" FILE_PATH)
    if(FILE_PATH)
        list(APPEND GRESOURCE_DEPENDENCIES ${CMAKE_SOURCE_DIR}/resources/${FILE_PATH})
    endif()
endforeach()

# Compile gresources to C source
set(GRESOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
add_custom_command(
    OUTPUT ${GRESOURCE_FILE}
    COMMAND ${GLIB_COMPILE_RESOURCES}
    ARGS --sourcedir=${CMAKE_SOURCE_DIR}/resources
         --target=${GRESOURCE_FILE}
         --generate-source
         ${GRESOURCE_XML}
    DEPENDS ${GRESOURCE_DEPENDENCIES}
    COMMENT "Compiling gresources"
)

# Automatically discover all source files recursively
# Note: GLOB is evaluated at configure time. Run cmake again after adding new source files.
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# Sort sources for consistent build order
list(SORT SOURCES)

# Add the generated gresources file
list(APPEND SOURCES ${GRESOURCE_FILE})

# Create the executable
add_executable(image-editor ${SOURCES})

# Link GTK3 libraries
target_link_libraries(image-editor ${GTK3_LIBRARIES})

# Link libxml2
target_link_libraries(image-editor ${LIBXML2_LIBRARIES})

# Link LZ4
target_link_libraries(image-editor ${LZ4_LIBRARIES})

# Link pthread for tile thread pool
target_link_libraries(image-editor pthread)

# Link Ocular static library
target_link_libraries(image-editor ${CMAKE_SOURCE_DIR}/lib/libocular.a)

# Link libjpeg if available
if(LIBJPEG_FOUND)
    if(LIBJPEG_TARGET AND TARGET ${LIBJPEG_TARGET})
        # Link against the CMake target (when building from source via ExternalProject)
        target_link_libraries(image-editor ${LIBJPEG_TARGET})
        message(STATUS "Linking against libjpeg target: ${LIBJPEG_TARGET}")
        
        # Add dependency to ensure libjpeg is built before the application
        # The ExternalProject is named libjpeg_external
        if(TARGET libjpeg_external)
            add_dependencies(image-editor libjpeg_external)
        endif()
        
        message(STATUS "libjpeg shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBJPEG_LIBRARIES)
        # Link against pkg-config found library (system libjpeg)
        target_link_libraries(image-editor ${LIBJPEG_LIBRARIES})
        target_compile_options(image-editor PRIVATE ${LIBJPEG_CFLAGS_OTHER})
        message(STATUS "Linking against system libjpeg: ${LIBJPEG_LIBRARIES}")
    else()
        # Try to find and link jpeg library directly
        find_library(JPEG_LIB
            NAMES jpeg libjpeg
            PATHS "${CMAKE_SOURCE_DIR}/lib/libjpeg/.libs"
                  "${CMAKE_SOURCE_DIR}/lib/libjpeg/build"
                  /usr/lib
                  /usr/local/lib
                  /opt/local/lib
        )
        if(JPEG_LIB)
            target_link_libraries(image-editor ${JPEG_LIB})
            message(STATUS "Linking against libjpeg: ${JPEG_LIB}")
        else()
            message(WARNING "libjpeg library not found, but headers were found. JPEG plugin may not work.")
        endif()
    endif()
    target_compile_definitions(image-editor PRIVATE HAVE_LIBJPEG=1)
else()
    target_compile_definitions(image-editor PRIVATE HAVE_LIBJPEG=0)
endif()

# Link libpng if available
if(LIBPNG_FOUND)
    if(LIBPNG_TARGET AND TARGET ${LIBPNG_TARGET})
        # Link against the CMake target (when building from source via ExternalProject)
        # Explicitly set include directories to use installed headers (not system headers)
        # Use SYSTEM and BEFORE to ensure our headers are found before system headers from GTK3
        get_target_property(PNG_INCLUDE_DIRS ${LIBPNG_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(PNG_INCLUDE_DIRS)
            # Add as SYSTEM include so it's treated as a system header (won't generate warnings)
            # but use BEFORE to ensure it comes before other system includes
            target_include_directories(image-editor PRIVATE BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
            message(STATUS "Using libpng include directories (BEFORE SYSTEM): ${PNG_INCLUDE_DIRS}")
        endif()
        target_link_libraries(image-editor ${LIBPNG_TARGET})
        message(STATUS "Linking against libpng target: ${LIBPNG_TARGET}")
        
        # Link zlib if we're building it from source
        if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
            target_link_libraries(image-editor ${ZLIB_TARGET})
            message(STATUS "Linking against zlib target: ${ZLIB_TARGET}")
            # Add dependency to ensure zlib is built before the application
            if(TARGET zlib_external)
                add_dependencies(image-editor zlib_external)
            endif()
        elseif(ZLIB_FOUND AND ZLIB_LIBRARIES)
            # Link system zlib
            target_link_libraries(image-editor ${ZLIB_LIBRARIES})
            target_compile_options(image-editor PRIVATE ${ZLIB_CFLAGS_OTHER})
            message(STATUS "Linking against system zlib for libpng: ${ZLIB_LIBRARIES}")
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(image-editor ${ZLIB_LIBRARIES})
            message(STATUS "Linking against zlib for libpng: ${ZLIB_LIBRARIES}")
        endif()
        
        # Add dependency to ensure libpng is built before the application
        if(TARGET libpng_external)
            add_dependencies(image-editor libpng_external)
        endif()
        
        message(STATUS "libpng shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBPNG_LIBRARIES)
        # Link against pkg-config found library (system libpng)
        target_link_libraries(image-editor ${LIBPNG_LIBRARIES})
        target_compile_options(image-editor PRIVATE ${LIBPNG_CFLAGS_OTHER})
        message(STATUS "Linking against system libpng: ${LIBPNG_LIBRARIES}")
        # System libpng usually links zlib automatically, but ensure it's available
        if(ZLIB_FOUND AND ZLIB_LIBRARIES)
            target_link_libraries(image-editor ${ZLIB_LIBRARIES})
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(image-editor ${ZLIB_LIBRARIES})
        endif()
    else()
        # Try to find and link png library directly
        find_library(PNG_LIB
            NAMES png16 png libpng16 libpng
            PATHS "${CMAKE_SOURCE_DIR}/lib/libpng/.libs"
                  "${CMAKE_SOURCE_DIR}/lib/libpng/build"
                  /usr/lib
                  /usr/local/lib
                  /opt/local/lib
        )
        if(PNG_LIB)
            target_link_libraries(image-editor ${PNG_LIB})
            message(STATUS "Linking against libpng: ${PNG_LIB}")
            # Also link zlib
            if(ZLIB_FOUND AND ZLIB_LIBRARIES)
                target_link_libraries(image-editor ${ZLIB_LIBRARIES})
            elseif(ZLIB_LIBRARIES)
                target_link_libraries(image-editor ${ZLIB_LIBRARIES})
            endif()
        else()
            message(WARNING "libpng library not found, but headers were found. PNG plugin may not work.")
        endif()
    endif()
    target_compile_definitions(image-editor PRIVATE HAVE_LIBPNG=1)
else()
    target_compile_definitions(image-editor PRIVATE HAVE_LIBPNG=0)
endif()

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})
target_compile_options(image-editor PRIVATE ${LIBXML2_CFLAGS_OTHER})
target_compile_options(image-editor PRIVATE ${LZ4_CFLAGS_OTHER})

