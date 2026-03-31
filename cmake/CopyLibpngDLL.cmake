# CMake script to copy libpng DLL if it exists.
# Called from ExternalProject POST_BUILD. Uses copy_if_different so we do not
# overwrite bin when the file is already identical (avoids needless copies).

set(LIBPNG_INSTALL_DIR "@LIBPNG_INSTALL_DIR@")
set(LIBPNG_DLL_NAME "@LIBPNG_DLL_NAME@")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "@CMAKE_RUNTIME_OUTPUT_DIRECTORY@")

# Try bin/ first, then lib/
set(DLL_BIN_PATH "${LIBPNG_INSTALL_DIR}/bin/${LIBPNG_DLL_NAME}")
set(DLL_LIB_PATH "${LIBPNG_INSTALL_DIR}/lib/${LIBPNG_DLL_NAME}")

if(EXISTS "${DLL_BIN_PATH}")
    set(LIBPNG_DLL_SRC "${DLL_BIN_PATH}")
elseif(EXISTS "${DLL_LIB_PATH}")
    set(LIBPNG_DLL_SRC "${DLL_LIB_PATH}")
else()
    message(WARNING "libpng DLL not found in ${LIBPNG_INSTALL_DIR}/bin/ or ${LIBPNG_INSTALL_DIR}/lib/")
    return()
endif()

file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${LIBPNG_DLL_SRC}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    RESULT_VARIABLE _libpng_copy_rv
)
if(NOT _libpng_copy_rv EQUAL 0)
    message(WARNING "libpng: copy_if_different failed for ${LIBPNG_DLL_SRC}")
endif()

