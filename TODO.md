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

## Future Optimizations

### High Priority

- [ ] **SSE/AVX SIMD compositing** - Vectorize the pixel blending loop in
      `tile_worker_composite_pixels()`. Process 4-8 pixels per iteration using
      intrinsics for significant speedup on large images. Example:
      ```c
      // Process 4 pixels at once with SSE
      __m128i src = _mm_loadu_si128((__m128i*)&layer_row[x]);
      __m128i dst = _mm_loadu_si128((__m128i*)&tile_row[x]);
      // ... SIMD alpha blending ...
      _mm_storeu_si128((__m128i*)&tile_row[x], result);
      ```

      Consider using SIMD Everywhere library for cross-platform SIMD support:
      https://github.com/simd-everywhere/simde

### Medium Priority

- [ ] **Layer surface caching per tile** - Cache layer->tile intersection
      surfaces to avoid recomputing geometry every frame.

- [ ] **Blend mode support in worker threads** - Currently worker threads only
      support OVER blend. Add SIMD-optimized implementations for Multiply,
      Screen, Overlay, etc.

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
