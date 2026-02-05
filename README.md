# RasterLab

A full-featured image editor written in C.

## Supported File Formats

| Format | Extensions | Read/Write | Color / notes |
|--------|------------|------------|----------------|
| PNG — Portable Network Graphic | `.png` | Read, Write | RGB, RGBA |
| JPEG — Joint Photographic Experts Group | `.jpg`, `.jpeg` | Read, Write | RGB only (no alpha) |
| BMP — Windows Bitmap | `.bmp` | Read, Write | RGB, RGBA |
| WebP — Google WebP Image | `.webp` | Read, Write | RGB, RGBA; animated (read as layers, save static only) |
| TIFF — Tagged Image File Format | `.tif`, `.tiff` | Read, Write | RGB, RGBA |
| Netpbm — Portable Pixmap/Graymap/Bitmap/Arbitrary | `.ppm`, `.pgm`, `.pbm`, `.pam`, `.pnm` | Read only | PBM (1-bit), PGM (grayscale), PPM (RGB), PAM (RGBA) |
| TGA — True Vision Targa | `.tga` | Read only | RGB, RGBA |
| SGI — Silicon Graphics Image | `.rgb`, `.rgba`, `.sgi`, `.bw`, `.int`, `.inta` | Read only | RGB, RGBA, grayscale |
| RAS — Sun Raster Image | `.ras`, `.sun` | Read only | RGB, RGBA |
| PCX — ZSoft Paintbrush | `.pcx` | Read only | RGB (no alpha) |
| XPM — X PixMap | `.xpm` | Read only | RGB, RGBA (indexed) |
| XBM — X Bitmap | `.xbm`, `.h` | Read only | 1-bit monochrome |
| CUT — Dr. Halo | `.cut` | Read only | RGB (no alpha) |
| DEEP — TVPaint IFF DEEP Image | `.deep` | Read only | RGB, RGBA |
| HDR — Radiance RGBE | `.hdr`, `.rgbe`, `.zyze`, `.pic` | Read only | HDR RGB (no alpha) |
| FITS — Flexible Image Transport System | `.fits`, `.fit`, `.fts` | Read only | RGB / grayscale |
| DICOM — Digital Imaging and Communications in Medicine | `.dcm`, `.dicom` | Read only | Grayscale, RGB, palette, YBR; RLE/JPEG compressed; multi-frame (as layers) |
| PCD — Kodak Photo CD | `.pcd` | Read only | All resolutions |

## Features

### File Operations

- Open and save images in the formats listed above
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
