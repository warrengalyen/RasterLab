# BumpBuildNumber.cmake — increment persistent build counter and emit build_version.h
# Invoke: cmake -D BUILD_NUMBER_FILE=... -D BUILD_VERSION_HEADER=... -P BumpBuildNumber.cmake

if(NOT BUILD_NUMBER_FILE)
    message(FATAL_ERROR "BumpBuildNumber.cmake: BUILD_NUMBER_FILE not set")
endif()
if(NOT BUILD_VERSION_HEADER)
    message(FATAL_ERROR "BumpBuildNumber.cmake: BUILD_VERSION_HEADER not set")
endif()

set(_n 0)
if(EXISTS "${BUILD_NUMBER_FILE}")
    file(READ "${BUILD_NUMBER_FILE}" _content)
    string(STRIP "${_content}" _content)
    if(_content MATCHES "^[0-9]+$")
        set(_n "${_content}")
    endif()
endif()

math(EXPR _next "${_n} + 1")

file(WRITE "${BUILD_NUMBER_FILE}" "${_next}\n")

file(WRITE "${BUILD_VERSION_HEADER}"
"#ifndef RASTERLAB_BUILD_VERSION_H
#define RASTERLAB_BUILD_VERSION_H

#define RASTERLAB_BUILD_NUMBER ${_next}
#define RASTERLAB_BUILD_NUMBER_STR \"${_next}\"

#endif /* RASTERLAB_BUILD_VERSION_H */
")
