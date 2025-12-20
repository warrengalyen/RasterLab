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
    ui/ui_filter_effects.c
    ui/ui_filter_utils.c
    document.c
    panels.c
    ui/widgets/accordion.c
    ui/widgets/anchor_position_widget.c
    ui/widgets/curves_widget.c
    ui/widgets/filter_preview.c
    ui/widgets/filter_dialog.c
    ui/dialogs/canvas_size_dialog.c
    ui/dialogs/clouds_dialog.c
    ui/dialogs/color_balance_dialog.c
    ui/dialogs/curves_dialog.c
    ui/dialogs/gamma_dialog.c
    ui/dialogs/new_layer_dialog.c
    ui/dialogs/palettize_dialog.c
    ui/dialogs/recovery_dialog.c
    ui/dialogs/retinex_dialog.c
    tools.c
    tools/tool_manager.c
    tools/tool_hand.c
    tools/tool_zoom.c
    tools/tool_move.c
    tools/tool_brush.c
    tools/tool_eraser.c
    tools/tool_paintbucket.c
    tool_options.c
    command.c
    ui/tools_panel.c
    ui/tool_options_panel.c
    ui/layers_panel.c
    filters/filters.c
    ui/filters/filter_utils.c
    ui/filters/filter_grayscale.c
    ui/filters/filter_vibrance.c
    ui/filters/filter_whitebalance.c
    ui/filters/filter_equalize.c
    ui/filters/filter_stretch.c
    ui/filters/filter_backlight.c
    ui/filters/filter_sepia.c
    ui/filters/filter_gamma.c
    ui/filters/filter_colorbalance.c
    ui/filters/filter_curves.c
    ui/filters/filter_exposure.c
    ui/filters/filter_color_invert.c
    ui/filters/filter_highlight_shadow_tint.c
    ui/filters/filter_hsl.c
    ui/filters/filter_luminance_threshold.c
    ui/filters/filter_monochrome.c
    ui/filters/filter_palettize.c
    ui/filters/filter_posterize.c
    ui/filters/filter_chroma_key.c
    ui/filters/filter_brightness_contrast.c
    ui/filters/filter_shadow_highlights.c
    ui/filters/filter_temperature.c
    ui/filters/filter_auto_whitebalance.c
    ui/filters/filter_auto_contrast.c
    ui/filters/filter_auto_gamma.c
    ui/filters/filter_auto_level.c
    ui/filters/filter_auto_threshold.c
    ui/filters/filter_dehaze.c
    ui/filters/filter_average_blur.c
    ui/filters/filter_gaussian_blur.c
    ui/filters/filter_box_blur.c
    ui/filters/filter_median_blur.c
    ui/filters/filter_motion_blur.c
    ui/filters/filter_radial_blur.c
    ui/filters/filter_surface_blur.c
    ui/filters/filter_zoom_blur.c
    ui/filters/filter_exponential_blur.c
    ui/filters/filter_film_grain.c
    ui/filters/filter_frosted_glass.c
    ui/filters/filter_oil_paint.c
    ui/filters/filter_pointillize.c
    ui/filters/filter_canny_edge.c
    ui/filters/filter_color_halftone.c
    ui/filters/filter_crystallize.c
    ui/filters/filter_fragment.c
    ui/filters/filter_gradient_edge.c
    ui/filters/filter_laplacian_edge.c
    ui/filters/filter_mosaic.c
    ui/filters/filter_prewitt_edge.c
    ui/filters/filter_radial_blur.c
    ui/filters/filter_relief.c
    ui/filters/filter_render_clouds.c
    ui/filters/filter_retinex.c
    ui/filters/filter_roberts_edge.c
    ui/filters/filter_sobel_edge.c
    render/render_utils.c
    render/layer.c
    render/compositor.c
    render/dirty.c
    render/tile.c
    render/tile_thread_pool.c
    render/tile_worker.c
    render/mipmap.c
    app/recent_files.c
    app/autosave.c
    app/settings.c
    ${GRESOURCE_FILE}
)

# Create the executable
add_executable(image-editor ${SOURCES})

# Link GTK3 libraries
target_link_libraries(image-editor ${GTK3_LIBRARIES})

# Link libxml2
target_link_libraries(image-editor ${LIBXML2_LIBRARIES})

# Link pthread for tile thread pool
target_link_libraries(image-editor pthread)

# Link Ocular static library
target_link_libraries(image-editor ${CMAKE_SOURCE_DIR}/lib/libocular.a)

# Add compile flags from pkg-config
target_compile_options(image-editor PRIVATE ${GTK3_CFLAGS_OTHER})
target_compile_options(image-editor PRIVATE ${LIBXML2_CFLAGS_OTHER})

