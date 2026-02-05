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

set(SOURCES
    app/autosave.c
    app/recent_files.c
    app/settings.c
    command.c
    commands/command_image.c
    commands/command_layer.c
    commands/command_move.c
    commands/command_selection.c
    document.c
    filters/filters.c
    io/image_io.c
    main.c
    panels.c
    plugins/builtin_plugins.c
    plugins/format_registry.c
    plugins/plugin_host_api.c
    plugins/plugin_bmp.c
    plugins/plugin_jpeg.c
    plugins/plugin_loader.c
    plugins/plugin_netpbm.c
    plugins/plugin_pcx.c
    plugins/plugin_pcd.c
    plugins/plugin_png.c
    plugins/plugin_ras.c
    plugins/plugin_sgi.c
    plugins/plugin_tga.c
    plugins/plugin_tiff.c
    plugins/plugin_xbm.c
    plugins/plugin_xpm.c
    plugins/plugin_cut.c
    plugins/plugin_deep.c
    plugins/plugin_hdr.c
    plugins/plugin_fits.c
    plugins/plugin_dicom.c
    plugins/plugin_webp.c
    render/compositor.c
    render/dirty.c
    render/layer.c
    render/mipmap.c
    render/render_utils.c
    render/tile.c
    render/tile_thread_pool.c
    render/tile_worker.c
    selection.c
    selection/selection_mask.c
    selection/selection_render.c
    selection/selection_undo.c
    selection/selection_undo_helpers.c
    tone_mapping.c
    tool_options.c
    tools.c
    tools/tool_brush.c
    tools/tool_eraser.c
    tools/tool_hand.c
    tools/tool_manager.c
    tools/tool_move.c
    tools/tool_paintbucket.c
    tools/tool_colorpicker.c
    tools/tool_pencil.c
    tools/tool_rect_select.c
    tools/tool_ellipse_select.c
    tools/tool_zoom.c
    ui.c
    ui/dialogs/beeps_dialog.c
    ui/dialogs/canvas_size_dialog.c
    ui/dialogs/hdr_image_dialog.c
    ui/dialogs/clouds_dialog.c
    ui/dialogs/color_balance_dialog.c
    ui/dialogs/color_chooser_dialog.c
    ui/dialogs/convolution_dialog.c
    ui/dialogs/curves_dialog.c
    ui/dialogs/formats/bmp_options_dialog.c
    ui/dialogs/formats/jpeg_options_dialog.c
    ui/dialogs/formats/png_options_dialog.c
    ui/dialogs/formats/tiff_options_dialog.c
    ui/dialogs/formats/webp_options_dialog.c
    ui/dialogs/gamma_dialog.c
    ui/dialogs/new_image_dialog.c
    ui/dialogs/new_layer_dialog.c
    ui/dialogs/palettize_dialog.c
    ui/dialogs/recovery_dialog.c
    ui/dialogs/retinex_dialog.c
    ui/dialogs/rotate_dialog.c
    ui/dialogs/save_options_dialog.c
    ui/dialogs/selection_radius_dialog.c
    ui/filters/filter_auto_contrast.c
    ui/filters/filter_auto_gamma.c
    ui/filters/filter_auto_level.c
    ui/filters/filter_auto_threshold.c
    ui/filters/filter_auto_whitebalance.c
    ui/filters/filter_average_blur.c
    ui/filters/filter_backlight.c
    ui/filters/filter_beeps.c
    ui/filters/filter_bilateral.c
    ui/filters/filter_box_blur.c
    ui/filters/filter_brightness_contrast.c
    ui/filters/filter_canny_edge.c
    ui/filters/filter_chroma_key.c
    ui/filters/filter_color_halftone.c
    ui/filters/filter_color_invert.c
    ui/filters/filter_colorbalance.c
    ui/filters/filter_convolution.c
    ui/filters/filter_crystallize.c
    ui/filters/filter_curves.c
    ui/filters/filter_dehaze.c
    ui/filters/filter_despeckle.c
    ui/filters/filter_dilate.c
    ui/filters/filter_distort_utils.c
    ui/filters/filter_equalize.c
    ui/filters/filter_erode.c
    ui/filters/filter_exponential_blur.c
    ui/filters/filter_exposure.c
    ui/filters/filter_film_grain.c
    ui/filters/filter_fragment.c
    ui/filters/filter_frosted_glass.c
    ui/filters/filter_gamma.c
    ui/filters/filter_gaussian_blur.c
    ui/filters/filter_gradient_edge.c
    ui/filters/filter_grayscale.c
    ui/filters/filter_guided.c
    ui/filters/filter_highlight_shadow_tint.c
    ui/filters/filter_highpass.c
    ui/filters/filter_hsl.c
    ui/filters/filter_kaleidoscope.c
    ui/filters/filter_laplacian_edge.c
    ui/filters/filter_luminance_threshold.c
    ui/filters/filter_max.c
    ui/filters/filter_median_blur.c
    ui/filters/filter_min.c
    ui/filters/filter_monochrome.c
    ui/filters/filter_mosaic.c
    ui/filters/filter_motion_blur.c
    ui/filters/filter_oil_paint.c
    ui/filters/filter_palettize.c
    ui/filters/filter_pinch.c
    ui/filters/filter_pointillize.c
    ui/filters/filter_polar_coordinates.c
    ui/filters/filter_posterize.c
    ui/filters/filter_prewitt_edge.c
    ui/filters/filter_radial_blur.c
    ui/filters/filter_relief.c
    ui/filters/filter_render_clouds.c
    ui/filters/filter_retinex.c
    ui/filters/filter_ripple.c
    ui/filters/filter_roberts_edge.c
    ui/filters/filter_sepia.c
    ui/filters/filter_shadow_highlights.c
    ui/filters/filter_sharpen.c
    ui/filters/filter_skin_smooth.c
    ui/filters/filter_split_toning.c
    ui/filters/filter_sobel_edge.c
    ui/filters/filter_spherize.c
    ui/filters/filter_stretch.c
    ui/filters/filter_surface_blur.c
    ui/filters/filter_temperature.c
    ui/filters/filter_twirl.c
    ui/filters/filter_unsharp.c
    ui/filters/filter_utils.c
    ui/filters/filter_vibrance.c
    ui/filters/filter_wave.c
    ui/filters/filter_whitebalance.c
    ui/filters/filter_zoom_blur.c
    ui/layers_panel.c
    ui/overview_panel.c
    ui/swatches.c
    ui/swatches_panel.c
    ui/tool_options_panel.c
    ui/tools_panel.c
    ui/workspace.c
    ui/ui_edit_menu.c
    ui/ui_file_menu.c
    ui/ui_filter.c
    ui/ui_filter_adjust.c
    ui/ui_filter_effects.c
    ui/ui_filter_utils.c
    ui/ui_image_menu.c
    ui/ui_layer_menu.c
    ui/ui_select_menu.c
    ui/ui_utils.c
    ui/ui_view_menu.c
    ui/widgets/accordion.c
    ui/widgets/anchor_position_widget.c
    ui/widgets/curves_widget.c
    ui/widgets/filter_dialog.c
    ui/widgets/filter_preview.c
    ui/widgets/hsv_color_wheel.c
    ui/widgets/hsv_scale.c
    ui/widgets/rgb_scale.c
    ui/widgets/swatches_widget.c
    ui/widgets/vertical_spin_button.c
    undo/undo_disk.c
)

# Add the generated gresources file
list(APPEND SOURCES ${GRESOURCE_FILE})

# Add Windows resource file for application icon
if(WIN32)
    enable_language(RC)
    
    set(RESOURCE_FILE ${CMAKE_SOURCE_DIR}/resources/rasterlab.rc)
    
    list(APPEND SOURCES ${RESOURCE_FILE})
endif()

# Create the executable
add_executable(rasterlab ${SOURCES})

# Link GTK3 libraries
target_link_libraries(rasterlab ${GTK3_LIBRARIES})

# Link libxml2
target_link_libraries(rasterlab ${LIBXML2_LIBRARIES})

# Link LZ4
target_link_libraries(rasterlab ${LZ4_LIBRARIES})

# Link pthread for tile thread pool
target_link_libraries(rasterlab pthread)

# Link Ocular static library
target_link_libraries(rasterlab ${CMAKE_SOURCE_DIR}/lib/libocular.a)

# Link libjpeg if available
if(LIBJPEG_FOUND)
    if(LIBJPEG_TARGET AND TARGET ${LIBJPEG_TARGET})
        # Link against the CMake target (when building from source via ExternalProject)
        target_link_libraries(rasterlab ${LIBJPEG_TARGET})
        message(STATUS "Linking against libjpeg target: ${LIBJPEG_TARGET}")
        
        # Add dependency to ensure libjpeg is built before the application
        # The ExternalProject is named libjpeg_external
        if(TARGET libjpeg_external)
            add_dependencies(rasterlab libjpeg_external)
        endif()
        
        message(STATUS "libjpeg shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBJPEG_LIBRARIES)
        # Link against pkg-config found library (system libjpeg)
        target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
        target_compile_options(rasterlab PRIVATE ${LIBJPEG_CFLAGS_OTHER})
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
            target_link_libraries(rasterlab ${JPEG_LIB})
            message(STATUS "Linking against libjpeg: ${JPEG_LIB}")
        else()
            message(WARNING "libjpeg library not found, but headers were found. JPEG plugin may not work.")
        endif()
    endif()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBJPEG=1)
else()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBJPEG=0)
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
            target_include_directories(rasterlab PRIVATE BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
            message(STATUS "Using libpng include directories (BEFORE SYSTEM): ${PNG_INCLUDE_DIRS}")
        endif()
        target_link_libraries(rasterlab ${LIBPNG_TARGET})
        message(STATUS "Linking against libpng target: ${LIBPNG_TARGET}")
        
        # Link zlib if we're building it from source
        if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
            target_link_libraries(rasterlab ${ZLIB_TARGET})
            message(STATUS "Linking against zlib target: ${ZLIB_TARGET}")
            # Add dependency to ensure zlib is built before the application
            if(TARGET zlib_external)
                add_dependencies(rasterlab zlib_external)
            endif()
        elseif(ZLIB_FOUND AND ZLIB_LIBRARIES)
            # Link system zlib
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            target_compile_options(rasterlab PRIVATE ${ZLIB_CFLAGS_OTHER})
            message(STATUS "Linking against system zlib for libpng: ${ZLIB_LIBRARIES}")
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            message(STATUS "Linking against zlib for libpng: ${ZLIB_LIBRARIES}")
        endif()
        
        # Add dependency to ensure libpng is built before the application
        if(TARGET libpng_external)
            add_dependencies(rasterlab libpng_external)
        endif()
        
        message(STATUS "libpng shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBPNG_LIBRARIES)
        # Link against pkg-config found library (system libpng)
        target_link_libraries(rasterlab ${LIBPNG_LIBRARIES})
        target_compile_options(rasterlab PRIVATE ${LIBPNG_CFLAGS_OTHER})
        message(STATUS "Linking against system libpng: ${LIBPNG_LIBRARIES}")
        # System libpng usually links zlib automatically, but ensure it's available
        if(ZLIB_FOUND AND ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
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
            target_link_libraries(rasterlab ${PNG_LIB})
            message(STATUS "Linking against libpng: ${PNG_LIB}")
            # Also link zlib
            if(ZLIB_FOUND AND ZLIB_LIBRARIES)
                target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            elseif(ZLIB_LIBRARIES)
                target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            endif()
        else()
            message(WARNING "libpng library not found, but headers were found. PNG plugin may not work.")
        endif()
    endif()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBPNG=1)
else()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBPNG=0)
endif()

# Link libwebp if available
if(LIBWEBP_FOUND)
    if(LIBWEBP_TARGET AND TARGET ${LIBWEBP_TARGET})
        # Link against the CMake target (when building from source via ExternalProject)
        # Explicitly set include directories to use installed headers (not system headers)
        get_target_property(WEBP_INCLUDE_DIRS ${LIBWEBP_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(WEBP_INCLUDE_DIRS)
            # Add as SYSTEM include so it's treated as a system header (won't generate warnings)
            # but use BEFORE to ensure it comes before other system includes
            target_include_directories(rasterlab PRIVATE BEFORE SYSTEM ${WEBP_INCLUDE_DIRS})
            message(STATUS "Using libwebp include directories (BEFORE SYSTEM): ${WEBP_INCLUDE_DIRS}")
        endif()
        target_link_libraries(rasterlab ${LIBWEBP_TARGET} webpdemux)
        message(STATUS "Linking against libwebp target: ${LIBWEBP_TARGET} and webpdemux")
        
        # Add dependency to ensure libwebp is built before the application
        if(TARGET libwebp_external)
            add_dependencies(rasterlab libwebp_external)
        endif()
        
        message(STATUS "libwebp shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBWEBP_LIBRARIES)
        # Link against pkg-config found library (system libwebp)
        target_link_libraries(rasterlab ${LIBWEBP_LIBRARIES})
        target_compile_options(rasterlab PRIVATE ${LIBWEBP_CFLAGS_OTHER})
        message(STATUS "Linking against system libwebp: ${LIBWEBP_LIBRARIES}")
        # Also link webpdemux if available
        if(LIBWEBPDEMUX_FOUND AND LIBWEBPDEMUX_LIBRARIES)
            target_link_libraries(rasterlab ${LIBWEBPDEMUX_LIBRARIES})
            target_compile_options(rasterlab PRIVATE ${LIBWEBPDEMUX_CFLAGS_OTHER})
            message(STATUS "Linking against system libwebpdemux: ${LIBWEBPDEMUX_LIBRARIES}")
        endif()
    else()
        # Try to find and link webp library directly
        find_library(WEBP_LIB
            NAMES webp libwebp
            PATHS "${CMAKE_SOURCE_DIR}/lib/libwebp/.libs"
                  "${CMAKE_SOURCE_DIR}/lib/libwebp/build"
                  /usr/lib
                  /usr/local/lib
                  /opt/local/lib
        )
        if(WEBP_LIB)
            target_link_libraries(rasterlab ${WEBP_LIB})
            # Also try to find webpdemux
            find_library(WEBPDEMUX_LIB
                NAMES webpdemux libwebpdemux
                PATHS "${CMAKE_SOURCE_DIR}/lib/libwebp/.libs"
                      "${CMAKE_SOURCE_DIR}/lib/libwebp/build"
                      /usr/lib
                      /usr/local/lib
                      /opt/local/lib
            )
            if(WEBPDEMUX_LIB)
                target_link_libraries(rasterlab ${WEBPDEMUX_LIB})
                message(STATUS "Linking against libwebp: ${WEBP_LIB} and ${WEBPDEMUX_LIB}")
            else()
                target_link_libraries(rasterlab ${WEBP_LIB})
                message(STATUS "Linking against libwebp: ${WEBP_LIB}")
                message(WARNING "libwebpdemux not found. Animated WebP support may not work.")
            endif()
        else()
            message(WARNING "libwebp library not found, but headers were found. WebP plugin may not work.")
        endif()
    endif()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBWEBP=1)
else()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBWEBP=0)
endif()

# Link libheif if available
if(LIBHEIF_FOUND)
    if(LIBHEIF_TARGET AND TARGET ${LIBHEIF_TARGET})
        get_target_property(HEIF_INCLUDE_DIRS ${LIBHEIF_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(HEIF_INCLUDE_DIRS)
            target_include_directories(rasterlab PRIVATE BEFORE SYSTEM ${HEIF_INCLUDE_DIRS})
            message(STATUS "Using libheif include directories (BEFORE SYSTEM): ${HEIF_INCLUDE_DIRS}")
        endif()
        target_link_libraries(rasterlab ${LIBHEIF_TARGET})
        message(STATUS "Linking against libheif target: ${LIBHEIF_TARGET}")
        
        if(TARGET libheif_external)
            add_dependencies(rasterlab libheif_external)
        endif()
        
        message(STATUS "libheif shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBHEIF_LIBRARIES)
        target_link_libraries(rasterlab ${LIBHEIF_LIBRARIES})
        target_compile_options(rasterlab PRIVATE ${LIBHEIF_CFLAGS_OTHER})
        message(STATUS "Linking against system libheif: ${LIBHEIF_LIBRARIES}")
    else()
        find_library(HEIF_LIB
            NAMES heif libheif
            PATHS "${CMAKE_SOURCE_DIR}/lib/libheif"
                  /usr/lib
                  /usr/local/lib
                  /opt/local/lib
        )
        if(HEIF_LIB)
            target_link_libraries(rasterlab ${HEIF_LIB})
            message(STATUS "Linking against libheif: ${HEIF_LIB}")
        else()
            message(WARNING "libheif library not found, but headers were found. HEIF/AVIF plugin may not work.")
        endif()
    endif()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBHEIF=1)
else()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBHEIF=0)
endif()

# Link libtiff if available
if(LIBTIFF_FOUND)
    if(LIBTIFF_TARGET AND TARGET ${LIBTIFF_TARGET})
        # Link against the CMake target (when building from source via ExternalProject)
        # Explicitly set include directories to use installed headers (not system headers)
        get_target_property(TIFF_INCLUDE_DIRS ${LIBTIFF_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(TIFF_INCLUDE_DIRS)
            # Add as SYSTEM include so it's treated as a system header (won't generate warnings)
            # but use BEFORE to ensure it comes before other system includes
            target_include_directories(rasterlab PRIVATE BEFORE SYSTEM ${TIFF_INCLUDE_DIRS})
            message(STATUS "Using libtiff include directories (BEFORE SYSTEM): ${TIFF_INCLUDE_DIRS}")
        endif()
        target_link_libraries(rasterlab ${LIBTIFF_TARGET})
        message(STATUS "Linking against libtiff target: ${LIBTIFF_TARGET}")
        
        # Link zlib if we're building it from source (libtiff depends on it)
        if(ZLIB_TARGET AND TARGET ${ZLIB_TARGET})
            target_link_libraries(rasterlab ${ZLIB_TARGET})
            message(STATUS "Linking against zlib target for libtiff: ${ZLIB_TARGET}")
            # Add dependency to ensure zlib is built before the application
            if(TARGET zlib_external)
                add_dependencies(rasterlab zlib_external)
            endif()
        elseif(ZLIB_FOUND AND ZLIB_LIBRARIES)
            # Link system zlib
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            target_compile_options(rasterlab PRIVATE ${ZLIB_CFLAGS_OTHER})
            message(STATUS "Linking against system zlib for libtiff: ${ZLIB_LIBRARIES}")
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            message(STATUS "Linking against zlib for libtiff: ${ZLIB_LIBRARIES}")
        endif()
        
        # Link jpeg if we're building it from source (libtiff may depend on it)
        if(LIBJPEG_TARGET AND TARGET ${LIBJPEG_TARGET})
            target_link_libraries(rasterlab ${LIBJPEG_TARGET})
            message(STATUS "Linking against libjpeg target for libtiff: ${LIBJPEG_TARGET}")
            # Add dependency to ensure libjpeg is built before the application
            if(TARGET libjpeg_external)
                add_dependencies(rasterlab libjpeg_external)
            endif()
        elseif(LIBJPEG_FOUND AND LIBJPEG_LIBRARIES)
            # Link system libjpeg
            target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
            target_compile_options(rasterlab PRIVATE ${LIBJPEG_CFLAGS_OTHER})
            message(STATUS "Linking against system libjpeg for libtiff: ${LIBJPEG_LIBRARIES}")
        elseif(LIBJPEG_LIBRARIES)
            target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
            message(STATUS "Linking against libjpeg for libtiff: ${LIBJPEG_LIBRARIES}")
        endif()
        
        # Add dependency to ensure libtiff is built before the application
        if(TARGET libtiff_external)
            add_dependencies(rasterlab libtiff_external)
        endif()
        
        message(STATUS "libtiff shared library will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    elseif(LIBTIFF_LIBRARIES)
        # Link against pkg-config found library (system libtiff)
        target_link_libraries(rasterlab ${LIBTIFF_LIBRARIES})
        target_compile_options(rasterlab PRIVATE ${LIBTIFF_CFLAGS_OTHER})
        message(STATUS "Linking against system libtiff: ${LIBTIFF_LIBRARIES}")
        # System libtiff usually links zlib and jpeg automatically, but ensure they're available
        if(ZLIB_FOUND AND ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
        elseif(ZLIB_LIBRARIES)
            target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
        endif()
        if(LIBJPEG_FOUND AND LIBJPEG_LIBRARIES)
            target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
        elseif(LIBJPEG_LIBRARIES)
            target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
        endif()
    else()
        # Try to find and link tiff library directly
        find_library(TIFF_LIB
            NAMES tiff libtiff
            PATHS "${CMAKE_SOURCE_DIR}/lib/libtiff/.libs"
                  "${CMAKE_SOURCE_DIR}/lib/libtiff/build"
                  /usr/lib
                  /usr/local/lib
                  /opt/local/lib
        )
        if(TIFF_LIB)
            target_link_libraries(rasterlab ${TIFF_LIB})
            message(STATUS "Linking against libtiff: ${TIFF_LIB}")
            # Also link zlib and jpeg if available
            if(ZLIB_FOUND AND ZLIB_LIBRARIES)
                target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            elseif(ZLIB_LIBRARIES)
                target_link_libraries(rasterlab ${ZLIB_LIBRARIES})
            endif()
            if(LIBJPEG_FOUND AND LIBJPEG_LIBRARIES)
                target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
            elseif(LIBJPEG_LIBRARIES)
                target_link_libraries(rasterlab ${LIBJPEG_LIBRARIES})
            endif()
        else()
            message(WARNING "libtiff library not found, but headers were found. TIFF plugin may not work.")
        endif()
    endif()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBTIFF=1)
else()
    target_compile_definitions(rasterlab PRIVATE HAVE_LIBTIFF=0)
endif()

# Add compile flags from pkg-config
target_compile_options(rasterlab PRIVATE ${GTK3_CFLAGS_OTHER})
target_compile_options(rasterlab PRIVATE ${LIBXML2_CFLAGS_OTHER})
target_compile_options(rasterlab PRIVATE ${LZ4_CFLAGS_OTHER})

