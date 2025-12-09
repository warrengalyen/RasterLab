# Find glib-compile-resources for gresources compilation
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

# Compile gresources to C source
set(GRESOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
add_custom_command(
    OUTPUT ${GRESOURCE_FILE}
    COMMAND ${GLIB_COMPILE_RESOURCES}
    ARGS --sourcedir=${CMAKE_SOURCE_DIR}/resources
         --target=${GRESOURCE_FILE}
         --generate-source
         ${CMAKE_SOURCE_DIR}/resources/gresources.xml
    DEPENDS ${CMAKE_SOURCE_DIR}/resources/gresources.xml
    COMMENT "Compiling gresources"
)

# Source files
set(SOURCES
    main.c
    ui.c
    document.c
    panels.c
    ui/widgets/accordion.c
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

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})

