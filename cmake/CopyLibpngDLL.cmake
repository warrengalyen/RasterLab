# CMake script to copy libpng DLL if it exists
# This script is called from ExternalProject POST_BUILD to copy the DLL

set(LIBPNG_INSTALL_DIR "@LIBPNG_INSTALL_DIR@")
set(LIBPNG_DLL_NAME "@LIBPNG_DLL_NAME@")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "@CMAKE_RUNTIME_OUTPUT_DIRECTORY@")

# Try bin/ first, then lib/
set(DLL_BIN_PATH "${LIBPNG_INSTALL_DIR}/bin/${LIBPNG_DLL_NAME}")
set(DLL_LIB_PATH "${LIBPNG_INSTALL_DIR}/lib/${LIBPNG_DLL_NAME}")

if(EXISTS "${DLL_BIN_PATH}")
    message(STATUS "Copying libpng DLL from bin/: ${DLL_BIN_PATH}")
    file(COPY "${DLL_BIN_PATH}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
elseif(EXISTS "${DLL_LIB_PATH}")
    message(STATUS "Copying libpng DLL from lib/: ${DLL_LIB_PATH}")
    file(COPY "${DLL_LIB_PATH}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
else()
    message(WARNING "libpng DLL not found in ${LIBPNG_INSTALL_DIR}/bin/ or ${LIBPNG_INSTALL_DIR}/lib/")
endif()

