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

# Source files
set(SOURCES
    main.c
    ui.c
    ui/ui_filter.c
    ui/ui_filter_adjust.c
    document.c
    panels.c
    ui/widgets/accordion.c
    ui/widgets/filter_preview.c
    ui/widgets/filter_dialog.c
    ui/widgets/gamma_dialog.c
    ui/widgets/color_balance_dialog.c
    tools.c
    tools/tool_manager.c
    tools/tool_move.c
    tools/tool_brush.c
    tools/tool_eraser.c
    tools/tool_fill.c
    tool_options.c
    command.c
    ui/tools_panel.c
    ui/tool_options_panel.c
    ui/layers_panel.c
    filters/filters.c
    ui/filters/filter_grayscale.c
    ui/filters/filter_vibrance.c
    ui/filters/filter_whitebalance.c
    ui/filters/filter_equalize.c
    ui/filters/filter_stretch.c
    ui/filters/filter_backlight.c
    ui/filters/filter_sepia.c
    ui/filters/filter_gamma.c
    ui/filters/filter_colorbalance.c
    ui/filters/filter_exposure.c
    ui/filters/filter_hsl.c
    ui/filters/filter_brightness_contrast.c
    ui/filters/filter_shadow_highlights.c
    ui/filters/filter_temperature.c
    render/render_utils.c
    render/layer.c
    render/compositor.c
    render/dirty.c
    render/tile.c
    ${GRESOURCE_FILE}
)

# Create the executable
add_executable(image-editor ${SOURCES})

# Link GTK3 libraries
target_link_libraries(image-editor ${GTK3_LIBRARIES})

# Link Ocular static library
target_link_libraries(image-editor ${CMAKE_SOURCE_DIR}/lib/libocular.a)

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})

