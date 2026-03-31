# CMake script to copy OpenEXR (and Imath) DLLs/shared libs to bin directory.
# Called from ExternalProject POST_BUILD. Compatible with CMake 3.10+.
# Uses copy_if_different so files already present in bin with identical content
# are not copied again.

set(OPENEXR_INSTALL_DIR "@OPENEXR_INSTALL_DIR@")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "@CMAKE_RUNTIME_OUTPUT_DIRECTORY@")

file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(OPENEXR_BIN "${OPENEXR_INSTALL_DIR}/bin")
    if(EXISTS "${OPENEXR_BIN}")
        file(GLOB OPENEXR_DLLS "${OPENEXR_BIN}/*.dll")
        foreach(DLL ${OPENEXR_DLLS})
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DLL}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                RESULT_VARIABLE _openexr_copy_rv
            )
            if(NOT _openexr_copy_rv EQUAL 0)
                message(WARNING "OpenEXR: copy_if_different failed for ${DLL}")
            endif()
        endforeach()
    endif()
else()
    set(OPENEXR_LIB "${OPENEXR_INSTALL_DIR}/lib")
    if(EXISTS "${OPENEXR_LIB}")
        file(GLOB OPENEXR_SOS "${OPENEXR_LIB}/*.so" "${OPENEXR_LIB}/*.so.*" "${OPENEXR_LIB}/*.dylib" "${OPENEXR_LIB}/*.dylib.*")
        foreach(LIB ${OPENEXR_SOS})
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${LIB}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
                RESULT_VARIABLE _openexr_copy_rv
            )
            if(NOT _openexr_copy_rv EQUAL 0)
                message(WARNING "OpenEXR: copy_if_different failed for ${LIB}")
            endif()
        endforeach()
    endif()
endif()
