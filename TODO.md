# Tile-Based Rendering Performance Improvements

## Completed

- [x] **Background-threaded tile compositing** - Worker threads composite dirty tiles
      into pixel buffers while main thread handles Cairo surfaces (thread-safe design)

- [x] **Tile priority queue** - Tiles closest to viewport center are composited first
      using `g_thread_pool_set_sort_function()`. Priority = distance² from viewport center.
      Improves perceived performance during scrolling - users see central content first.

- [x] **Tile-based mipmapping for fast zoom** - Pre-computed downscaled tile versions at
      50%, 25%, 12.5%, 6.25% stored in each Tile structure. Mipmaps auto-generated after
      tile compositing. When zoomed out (<100%), rendering uses tile mipmaps instead of
      expensive per-pixel layer scaling. `tile_get_mipmap_for_zoom()` selects best level.

- [x] **Dirty region coalescing** - Combine multiple small dirty regions into
      larger rectangles to reduce per-tile overhead. Implemented via `DirtyRegionList`
      which maintains individual dirty rectangles and coalesces them intelligently:
      - Merges overlapping rectangles
      - Merges nearby rectangles (within 16px by default)
      - Auto-coalesces when list exceeds 32 rectangles
      - Minimizes over-invalidation by checking waste ratio before merging

- [x] **Layer surface caching per tile** - Cache layer->tile intersection
      geometry to avoid recomputing bounds every frame. Implemented via
      `LayerTileIntersection` structure stored in per-tile hash table:
      - Caches intersection rectangle in document, tile-local, and layer coordinates
      - Self-invalidating: validates cached layer position/size before use
      - Automatically removes stale entries when layers are deleted
      - Used by main-thread `tile_composite()` (worker threads compute inline)

- [x] **SSE/AVX SIMD compositing** - Vectorize the pixel blending loop in
      `tile_worker_composite_pixels()`. Implemented via SIMDe library for
      cross-platform SSE2 support:
      - Processes 4 ARGB32 pixels per iteration using 128-bit registers
      - `simd_apply_opacity()` - applies layer opacity to 4 pixels at once
      - `simd_extract_alpha()` - broadcasts alpha channel to all components
      - `simd_blend_over()` - performs OVER blend for premultiplied alpha
      - `simd_composite_row()` - main entry point with scalar fallback for
        remaining 0-3 pixels
      - Uses `(x + 128) >> 8` approximation for division by 255 (faster, accurate)
      - SIMDe provides native SSE2 on x86 or portable fallback on other platforms

- [x] **Blend mode support in worker threads** - All blend modes now have
      SIMD-optimized implementations in worker threads via `simd_composite_row_blend()`:
      - All 27 Photoshop-compatible blend modes are supported
      - HSL-based component modes (Hue, Saturation, Color, Luminosity) use scalar
        implementations for accuracy since the RGB-to-HSL conversions are complex.
      - Each blend mode has both SIMD (4 pixels at once) and scalar fallback
      - `simd_composite_with_alpha()` handles alpha compositing for all modes
      - First visible layer always uses OVER blend (same as Cairo compositor)
      - Scalar fallbacks use exact formulas for precision on remaining pixels

## Future Optimizations

### High Priority

### Medium Priority

- [ ] **Opacity slider live preview** - Currently, canvas updates are deferred
      until slider release for responsiveness on large images. Investigate ways
      to provide live preview during drag without lag:
      - **Low-resolution preview**: Render at 1/2 or 1/4 resolution during drag,
        full resolution on release
      - **GPU-accelerated compositing**: Real-time compositing via OpenGL/Vulkan
        shaders would eliminate the bottleneck entirely
      - **Draw-time opacity approximation**: Instead of recompositing all tiles,
        adjust the layer's surface alpha during the draw callback. This would
        require special handling to blend the layer surface with adjusted alpha
        over the composite of layers below it. Complex but potentially very fast.
      - **Mipmap-based preview**: Use existing tile mipmaps for faster preview
        compositing during drag

### Lower Priority  

- [ ] **GPU-accelerated compositing** - Use OpenGL/Vulkan for tile compositing
      when available. Upload layer textures to GPU and composite in shaders.

- [ ] **Progressive loading** - Load tiles on-demand as viewport scrolls,
      showing low-res preview first then full resolution.

---

# Selection Preview Performance Improvements

Selection preview rendering with feathering is slow on large images due to several
bottlenecks in `tool_rect_select.c`, `tool_ellipse_select.c`, and `selection_mask.c`.

## Identified Bottlenecks

### 1. Full-document mask allocation

**Location:** `tool_rect_select.c:757`, `tool_ellipse_select.c:779`

```c
SelectionMask* preview_mask = selection_mask_new(doc->width, doc->height);
```

**Problem:** Allocates a full-size mask for the entire document even for small selections.
For a 4000x3000 image, this allocates ~12MB per preview draw operation.

### 2. Per-pixel mask filling loops

**Location:** `tool_rect_select.c:768-772`, `tool_ellipse_select.c:797-807`

**Problem:** Nested loops fill pixels one-by-one. Should use `memset` for rectangles
and optimized scanline rasterization for ellipses.

### 3. O(n²) distance field computation

**Location:** `selection_mask.c:1167-1276` (`selection_generate_feathered_preview`)

**Problem:** Computes signed distance field for the *entire* mask:

- `compute_distance_outside()` and `compute_distance_inside()` process every pixel
- Each pixel searches within `max_search_radius` (up to 200px) computing distances
- For a 4000x3000 image with 50px feather, this is ~600 million distance calculations

### 4. No caching of feathered previews

**Location:** Both selection tool files

**Problem:** Each redraw recomputes the entire feathered preview from scratch.
No caching even when selection bounds/parameters haven't changed.

### 5. Repeated allocations during drag

**Problem:** On each preview draw during selection editing:

1. Allocates full-size `SelectionMask`
2. Creates new `Selection` object
3. Fills mask pixel-by-pixel
4. Rebuilds combined mask
5. Regenerates feathered preview
6. Frees everything

## Proposed Optimizations

### High Priority

- [ ] **Bounding-box mask allocation** - Only allocate mask for
      `(selection_bounds + feather_radius)` instead of full document. Track offset
      for coordinate translation. Would reduce memory from O(doc_area) to O(selection_area).

- [ ] **Optimized distance transform** - Replace current O(n² × radius) algorithm with
      efficient separable distance transform (O(n) per dimension). Options:
      - Meijster's algorithm (exact Euclidean, O(n))
      - Felzenszwalb-Huttenlocher distance transform
      - Jump flooding algorithm (GPU-friendly approximation)

- [ ] **Preview result caching** - Cache the computed feathered preview surface and
      only regenerate when:
      - Selection bounds change
      - Feather radius changes
      - Selection mode (add/subtract/intersect) changes

      During marching ants animation, reuse cached surface.

### Medium Priority

- [ ] **Incremental feathering** - For resize operations, only recompute the changed
      edge regions rather than the entire selection.

- [ ] **Fast rectangle filling** - Replace per-pixel loops with `memset()` for solid
      rectangle regions:
      ```c
      for (int row = rect_y; row < rect_y + rect_h; row++) {
          memset(&mask[row * stride + rect_x], 255, rect_w);
      }
      ```

- [ ] **Optimized ellipse rasterization** - Use midpoint ellipse algorithm with
      scanline filling instead of per-pixel distance checks.

- [ ] **Deferred feathering computation** - Only compute feathering after a short
      delay (e.g., 100ms) when user stops resizing, showing unfeathered preview
      during active manipulation.

### Lower Priority

- [ ] **GPU-accelerated distance transform** - Compute distance field on GPU using
      compute shaders or jump flooding algorithm.

- [ ] **Approximate feathering** - For large feather radii, use approximation methods
      like multi-pass box blur on the binary mask edge.

- [ ] **Level-of-detail preview** - During active resize, show feathering at reduced
      resolution and refine when interaction stops.
