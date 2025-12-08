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
            ${CMAKE_SOURCE_DIR}/resources/move.png
            ${CMAKE_SOURCE_DIR}/resources/paintbrush.png
            ${CMAKE_SOURCE_DIR}/resources/eraser.png
            ${CMAKE_SOURCE_DIR}/resources/flood-fill.png
    COMMENT "Compiling gresources"
)

# Source files
set(SOURCES
    main.c
    ui.c
    document.c
    panels.c
    accordion.c
    tools.c
    tool_manager.c
    tool_move.c
    tool_brush.c
    tool_eraser.c
    tool_fill.c
    tool_options.c
    command.c
    ui/tools_panel.c
    ui/tool_options_panel.c
    ui/layers_panel.c
    ${GRESOURCE_FILE}
)

# Create the executable
add_executable(image-editor ${SOURCES})

# Link GTK3 libraries
target_link_libraries(image-editor ${GTK3_LIBRARIES})

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})

