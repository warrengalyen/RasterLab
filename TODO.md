# Tile-Based Rendering Performance Improvements

## Completed
- [x] **Background-threaded tile compositing** - Worker threads composite dirty tiles
      into pixel buffers while main thread handles Cairo surfaces (thread-safe design)

- [x] **Tile priority queue** - Tiles closest to viewport center are composited first
      using `g_thread_pool_set_sort_function()`. Priority = distance² from viewport center.
      Improves perceived performance during scrolling - users see central content first.

## Future Optimizations

### High Priority
- [ ] **Mipmapping for fast zoom** - Pre-compute downscaled tile versions at 50%, 25%, etc.
      to avoid expensive per-pixel scaling during zoom. Store mipmaps in tile structure.
      Use appropriate mipmap level based on current zoom factor.

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

### Medium Priority
- [ ] **Dirty region coalescing** - Combine multiple small dirty regions into
      larger rectangles to reduce per-tile overhead.

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
