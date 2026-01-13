# RasterLab

A full-featured image editor written in C.

## Features

### File Operations

- Open and save images (PNG, JPEG)
- Multi-document support with tabbed interface
- Autosave functionality
- Recent files tracking

### Drawing Tools

- **Brush Tool** - Paint with customizable size, opacity, hardness, flow, and spacing
- **Eraser Tool** - Erase with brush-like settings
- **Paint Bucket Tool** - Fill areas with color
- **Move Tool** - Move layers within the canvas
- **Rectangle Select Tool** - Create rectangular selections with multiple combine modes
- **Hand Tool** - Pan around the canvas
- **Zoom Tool** - Zoom in/out (10% to 800%)

### Layers

- Create, delete, duplicate, and reorder layers
- Layer opacity and visibility controls
- Layer locking
- **11 Blend Modes**: Normal, Darken, Multiply, Color Burn, Lighten, Screen, Color Dodge, Overlay, Soft Light, Hard Light, Difference
- Layer transformations: Flip horizontal/vertical, transpose
- Fit active layer to canvas, fit all layers to canvas
- Merge visible layers and flatten image

### Selection

- Pixel-based selection masks
- Selection combine modes: New, Add, Subtract, Intersect
- Selection feathering and antialiasing
- Invert selection
- Marching ants visualization

### Image Adjustments & Filters

- 60+ filters including:
  - **Color Adjustments**: Brightness/Contrast, Levels, Curves, Hue/Saturation/Lightness, Color Balance, Temperature, Vibrance, Exposure, Gamma, White Balance
  - **Blur Effects**: Gaussian, Motion, Radial, Zoom, Box, Bilateral, Surface, Guided, Exponential, Average
  - **Sharpen**: Unsharp Mask, High Pass
  - **Edge Detection**: Sobel, Canny, Prewitt, Roberts, Laplacian, Gradient
  - **Stylization**: Oil Paint, Posterize, Pointillize, Fragment, Mosaic, Crystallize, Film Grain, Relief
  - **Special Effects**: Chroma Key, Despeckle, Frosted Glass, Skin Smooth, Retinex, Dehaze, Backlight
  - **Morphology**: Dilate, Erode, Min, Max
  - **Color Effects**: Grayscale, Sepia, Monochrome, Color Invert, Color Halftone, Palettize
  - And many more...

### Canvas Operations

- Resize canvas with 9 anchor point positions
- Duplicate document
- Zoom controls (10% to 800%)

### Undo/Redo System

- Disk-backed undo journal with LZ4 compression
- Efficient storage for pixel operations
- Full undo/redo support for all operations

### Rendering

- Tile-based rendering system for high performance
- Efficient compositing for large images
- Multi-threaded tile processing

### User Interface

- Collapsible panels (Layers, Tools, Tool Options)
- Tool options panel with real-time parameter adjustment
- Layers panel with visual layer management
- Status bar
- Keyboard shortcuts

## License

GPLv3
