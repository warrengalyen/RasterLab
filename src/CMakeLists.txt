# Source files
set(SOURCES
    main.c
    ui.c
    document.c
    panels.c
    tools.c
    tool_manager.c
    tool_move.c
    tool_brush.c
    tool_eraser.c
    tool_fill.c
    command.c
)

# Create the executable
add_executable(image-editor ${SOURCES})

# Link GTK3 libraries
target_link_libraries(image-editor ${GTK3_LIBRARIES})

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})

