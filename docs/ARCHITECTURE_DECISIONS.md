# Windows Snip Tool — Architecture Decisions

## ADR-0001: Native application architecture

**Status:** Accepted
**Date:** 15 November 2026

### Decision

Build the application using native Win32 and C++20,
with Direct2D and DirectWrite for application rendering.

### Technology stack

| Technology | Responsibility |
|---|---|
| C++20 | Primary language |
| Win32 API | Window management, input, monitors and OS integration |
| Direct2D | 2D graphics rendering |
| DirectWrite | Text rendering and layout |
| Direct3D 11 | Graphics device integration |
| Windows Graphics Capture | Display and window capture |
| Windows Imaging Component | Image decoding and encoding |
| DirectComposition | Optional composition and animation |
| CMake | Build configuration |
| MSVC | Compiler |
| MSIX | Future application packaging |

### Rationale

- Uses the existing verified C++ development environment.
- Avoids installing an additional UI framework before development.
- Provides control over floating windows and transparent overlays.
- Supports custom rendering and annotation surfaces.
- Uses documented Windows desktop APIs.
- Keeps capture, image processing and UI modules independent.

### UI strategy

Create custom Windows 11-inspired controls and surfaces.

The application will reproduce the familiar Snipping Tool
interaction model without modifying Microsoft's application
or copying its proprietary implementation.

### Capture strategy

Use Windows Graphics Capture for supported display and
application-window acquisition.

Use Win32 APIs where lower-level window, monitor and
input handling is required.

### Image strategy

Use Windows Imaging Component for image codecs,
metadata and format conversion.

### Packaging strategy

MSIX packaging will be introduced after the application
foundation and core capture workflow are stable.

### Consequences

- Custom controls and UI behaviour must be implemented.
- Accessibility, keyboard navigation and scaling require
  explicit testing.
- Rendering and capture lifecycles must be managed carefully.
- No WinUI 3 dependency is required for the initial architecture.

### Reconsideration conditions

Revisit this decision only if implementation evidence
shows that a different framework provides a substantial
maintainability or capability advantage.

---

## Decision history

| ID | Decision | Status |
|---|---|---|
| ADR-0001 | Win32 + Direct2D + DirectWrite | Accepted |
