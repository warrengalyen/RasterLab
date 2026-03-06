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

- [x] **GPU-accelerated compositing** - OpenGL-based tile compositing using GLFW
      for context management. Provides significant speedup for opacity changes and
      live preview on large images:
      - Hidden GLFW window creates OpenGL context for off-screen rendering
      - Layer surfaces uploaded to GPU textures with smart caching (invalidated
        via `content_version` tracking when layer content changes)
      - Fragment shader with full blend mode support (all 27 Photoshop-compatible modes)
      - Ping-pong FBO rendering for blend modes that need destination pixel access
      - Uses premultiplied alpha throughout for correct Porter-Duff OVER compositing
      - HSL component modes (Hue, Saturation, Color, Luminosity) implemented in shader
      - Configurable via app settings: enable/disable GPU acceleration,
        select GPU device
      - Debug overlay shows GPU compositor statistics (tiles composited,
        memory usage, etc.)

## Future Optimizations

### High Priority

### Medium Priority

### Lower Priority  

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

- [x] **Bounding-box mask allocation** - Only allocate mask for
      `(selection_bounds + feather_radius)` instead of full document. Track offset
      for coordinate translation. Reduces memory from O(doc_area) to O(selection_area):
      - `SelectionMask` gains `offset_x`/`offset_y` fields (zero for full-document masks,
        non-zero for bounded masks — fully backward-compatible).
      - `selection_mask_new_bounded(offset_x, offset_y, width, height)` allocates only the
        sub-region: `(rect ± ceil(feather_radius) + 2)` clamped to document bounds.
      - Preview draw in `tool_rect_select.c`, `tool_ellipse_select.c`, and
        `tool_polygon_select.c` uses the bounded constructor. Rect/ellipse fill
        `Selection->mask` in local coordinates directly; polygon translates its
        vertex arrays to local coordinates before passing them to
        `selection_mask_fill_polygon()` (so Cairo rasterises into the small surface).
        Polygon exterior mode (`area_mode==1`) falls back to the full-document mask
        because inversion would create a spurious rectangular outline at the bounded
        mask boundary.
      - `selection_mask_render_outline()` translates local (x, y) → document space via
        `(x + offset_x, y + offset_y)` before calling `cairo_rectangle()`, preserving
        continuous marching-ants dash pattern using document coordinates.
      - `selection_generate_feathered_preview()` already accepts `mask_width/height/stride`
        as parameters so it processes only the bounded region without any other changes.
      - For a 4000×3000 document with a 200×200 selection and 50px feather, peak
        allocation drops from ~12 MB to ~(304×304 × 3 buffers) ≈ 270 KB — ~45× reduction.

- [x] **Optimized distance transform** - Replaced the O(width × height × radius)
      limited-search algorithm with the Felzenszwalb-Huttenlocher (FH) separable
      Euclidean Distance Transform:
      - **Algorithm:** `edt_1d()` — 1-D lower-envelope parabola sweep (FH 2012).
        Two passes suffice: one across rows, one down columns. Each pass is O(n).
        Total complexity: O(width × height) regardless of feather radius.
      - **`compute_edt()`** replaces both `compute_distance_outside()` and
        `compute_distance_inside()` with a unified function parameterised by
        `seed_is_nonzero`. Allocates a single workspace of O(max(width, height))
        shared across all rows and columns — no per-column `g_malloc`/`g_free`.
      - **`selection_generate_feathered_preview()`** simplified:
        - Removed `large_val`, `max_search_radius`, and the pre-initialization loop
        - Eliminated the `signed_dist` float buffer (signed distance computed inline)
        - Reduced temporary peak allocation from 3 × (width × height × 4B) to
          2 × (width × height × 4B) + tiny O(max_dim) workspace
      - **No radius cap** — the old code capped search at 200 px giving wrong
        feathering for larger radii; FH is exact at any radius.
      - For a 4000×3000 bounded mask (~1000×1000 after bounding-box optimisation),
        the EDT drops from ~600 M operations to ~2 M (300×).

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
