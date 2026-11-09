# Windows Scroll Capture

A native Windows C++ application designed to bring scrolling screenshot functionality to the Windows Snipping Tool workflow.

## Current Version

0.1.0 - Project initialization.

## Goals

- Integrate with the native Windows Snipping Tool capture workflow.
- Capture long, vertically scrolling content.
- Automatically scroll supported windows.
- Detect overlapping image regions.
- Stitch captured frames into one continuous screenshot.
- Export screenshots as PNG and JPEG.

## Technology Stack

- C++17
- Win32 API
- CMake
- Windows SDK
- Windows Graphics Capture
- OpenCV (planned)
- MSIX packaging

## Project Structure

```text
.
├── CMakeLists.txt
├── README.md
├── .gitignore
├── include/
│   └── scroller/
├── src/
├── tests/
├── packaging/
├── docs/
└── assets/
```

## Development Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Project initialization and Git setup | In progress |
| M1 | Snipping Tool integration and callback | Pending |
| M2 | Screen capture engine | Pending |
| M3 | Automatic scrolling engine | Pending |
| M4 | OpenCV image stitching | Pending |
| M5 | Floating capture toolbar | Pending |
| M6 | Export and packaging | Pending |

## Build

Use a Visual Studio Developer Command Prompt:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

## License

To be determined.