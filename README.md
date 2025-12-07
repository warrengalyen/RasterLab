# Image Editor

A GTK3-based image editor written in C.

## Project Structure

```
.
├── CMakeLists.txt          # Top-level CMake configuration
├── src/
│   ├── CMakeLists.txt      # Source build configuration
│   ├── main.c              # Application entry point
│   ├── ui.c                # UI system (window, menus, notebook)
│   └── document.c          # Document management and drawing
├── include/
│   ├── ui.h                # UI system header
│   └── document.h          # Document structure header
├── data/                   # Data files, assets, resources
└── README.md               # This file
```

## Requirements

- C compiler (GCC or Clang)
- CMake 3.10 or later
- GTK3 development libraries
- pkg-config

### Ubuntu/Debian

```bash
sudo apt-get install build-essential cmake pkg-config libgtk-3-dev
```

### Fedora

```bash
sudo dnf install gcc cmake pkg-config gtk3-devel
```

### macOS (with Homebrew)

```bash
brew install cmake pkg-config gtk+3
```

## Building

### Create and enter build directory

```bash
mkdir build && cd build
```

### Configure with CMake

```bash
cmake ..
```

### Build the project

```bash
make
```

### Run the application

```bash
./image-editor
```

## Features

### MDI (Multiple Document Interface)
- **GtkNotebook** widget displays multiple open image documents as tabs
- Each document tab can be closed individually via the close button
- Scrollable drawing areas for rendering images

### Menu System
- **File > Open** - Create a new untitled document
- **File > Close** - Close the current document
- **File > Exit** - Exit the application (Ctrl+Q)
- Keyboard shortcuts: Ctrl+O for Open, Ctrl+Q for Exit

### Document Management
- Each document holds:
  - Filename/identifier
  - Cairo surface for rendering
  - Modified flag
  - Associated drawing area widget
  - Scrollable view container
- Drawing area shows a placeholder grid when empty

## Development Notes

- The project uses C99 standard
- GTK3 is detected using pkg-config for portability
- **ui.c**: Manages window, menus, notebooks, and document tabs
- **document.c**: Handles document creation, drawing, and cleanup
- **ui.h** and **document.h**: Public interfaces for the UI system
- Future components can be added to the `include/` and `src/` directories
- Data files (images, UI resources, etc.) should be placed in `data/`

## Architecture

```
main.c
  └── AppContext (ui.h)
       ├── window (GtkWindow)
       ├── menu_bar (GtkMenuBar with File menu)
       └── notebook (GtkNotebook)
            └── [multiple tabs]
                 └── ImageDocument (document.h)
                      ├── scrolled_window (GtkScrolledWindow)
                      └── drawing_area (GtkDrawingArea)
```

## License

[Add license information here]

