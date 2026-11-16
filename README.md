# Windows Snip Tool

A native Windows screenshot application built with C++20.

The project aims to reproduce the familiar Windows Snipping Tool
experience while adding a dedicated scrolling screenshot capability.

## Technology Stack

- C++20
- Win32 API
- Direct2D
- DirectWrite
- Direct3D 11
- DXGI
- Windows Graphics Capture
- C++/WinRT
- Windows Imaging Component (WIC)
- CMake
- MSVC

## Current Implementation

- Native Windows application window
- Dark-themed Snipping Tool-style interface
- Screenshot and video mode selection UI
- Rectangle, Window, Full Screen and Freeform mode selection UI
- Capture delay selection UI (0, 3, 5 and 10 seconds)
- Direct2D rendering
- Application logging
- Direct3D 11 device initialization
- High-performance GPU selection through DXGI
- NVIDIA RTX 4050 hardware acceleration verified
- Direct3D-to-WinRT device interop
- Windows Graphics Capture support verification

**Note:** Actual frame acquisition, screenshot saving,
recording and scrolling capture are still under development.
The current mode selectors are UI functionality only.

## Requirements

- Windows 10 version 2004 or later (Windows Graphics Capture)
- Windows SDK 10.0.26100.0
- Visual Studio C++ desktop development tools
- CMake 3.25 or newer
- x64 build environment

## Build Instructions

Configure:

```powershell
cmake -S . -B build