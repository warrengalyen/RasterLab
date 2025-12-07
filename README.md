# Image Editor

A GTK3-based image editor written in C.

## Project Structure

```
.
├── CMakeLists.txt          # Top-level CMake configuration
├── src/
│   ├── CMakeLists.txt      # Source build configuration
│   └── main.c              # Application entry point
├── include/                # Header files (future expansion)
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

## Development Notes

- The project uses C99 standard
- GTK3 is detected using pkg-config for portability
- The main window is initialized with basic event handling
- Future components can be added to the `include/` and `src/` directories
- Data files (images, UI resources, etc.) should be placed in `data/`

## License

[Add license information here]

