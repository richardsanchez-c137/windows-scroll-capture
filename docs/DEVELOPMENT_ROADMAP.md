# Windows Snip Tool — Development Roadmap

**Roadmap version:** 1.0\
**Baseline date:** 15 November 2026\
**Project status:** Greenfield restart — planning baseline\
**Repository directory:** `D:\GitHub\Projects\Scroller for Windows Snipping Tool`\
**Product goal:** Build an independent Windows screenshot and screen-recording application that closely follows the interaction model of Windows 11 Snipping Tool and adds first-class scrolling capture.

> This roadmap is a tracking document, not a claim that any feature is already implemented. Every implementation item starts as **Not started**. A feature is not marked Done until its acceptance criteria have been tested on the target Windows installation.

---

## 1. Product definition

Build a native Windows application with a familiar Snipping Tool-style capture experience:

1. A compact capture toolbar with image/video modes, capture modes, delay and settings.
2. Full-screen selection overlays for rectangle and freeform capture, plus window and display selection.
3. A screenshot editor with markup, crop, text actions, copy, save and share.
4. Screen recording with audio options and basic trim/export tools.
5. A **Scroll** capture mode that captures a long page or document over multiple viewports and stitches the frames into one continuous image.
6. Settings, history/recents, reliable save behaviour, accessibility and an installable MSIX package.

### Product boundary

- The application is an independent implementation. It will not patch, inject into, or redistribute Microsoft's Snipping Tool executable, private assets or source code.
- We can reproduce familiar workflows and visual conventions, but should create our own code and assets.
- `Win + Shift + S` and `Print Screen` are owned/configured by Windows. We can offer an opt-in global-hotkey registration and a settings path for users to assign the shortcut; we must not promise that we can silently replace a Windows-reserved shortcut on every system.
- The goal is feature parity for the publicly documented capabilities below, not undocumented internal behaviour of Microsoft's app.

---

## 2. Evidence baseline: publicly documented Snipping Tool features

**Research cutoff:** 15 November 2026. The baseline below is compiled from Microsoft's current Snipping Tool support article and Microsoft feature announcements. Microsoft may roll features out gradually, vary availability by Windows build, app version, region or hardware, and change the app after this roadmap is written.

### 2.1 Core screenshot capture — confirmed in Microsoft Support

| ID | Feature | Documented behaviour | Roadmap priority | Status |
|---|---|---|---|---|
| MS-CAP-01 | Rectangle snip | Drag a rectangular region of the display. | P0 | Not started |
| MS-CAP-02 | Freeform snip | Draw a freeform boundary around screen content. | P1 | Not started |
| MS-CAP-03 | Window snip | Select an application window/dialog for capture. | P1 | Not started |
| MS-CAP-04 | Full-screen snip | Capture the entire screen/display. | P0 | Not started |
| MS-CAP-05 | Capture overlay | Dim the desktop and show selection affordances while choosing a region. | P0 | Not started |
| MS-CAP-06 | Capture toolbar | Compact toolbar to choose image capture or video recording and the capture mode. | P0 | Not started |
| MS-CAP-07 | Delayed capture | Delay capture by 1–5 seconds so menus or transient UI can be opened. | P1 | Not started |
| MS-CAP-08 | Cursor/multi-display behaviour | Handle pointer selection and screen coordinates correctly across displays. | P0 | Not started |
| MS-CAP-09 | Repeat last mode | Start a new snip using the previously selected mode. | P1 | Not started |

### 2.2 Screenshot annotation and editing — confirmed in Microsoft Support

| ID | Feature | Documented behaviour | Priority | Status |
|---|---|---|---|---|
| MS-EDIT-01 | Pen | Draw/write on or around a captured image. | P0 | Not started |
| MS-EDIT-02 | Highlighter | Highlight areas of an image. | P0 | Not started |
| MS-EDIT-03 | Shapes | Add shapes to an image. | P1 | Not started |
| MS-EDIT-03A | Draw & Hold shape recognition | Hold a drawn line/arrow/rectangle/oval briefly to convert the stroke into a neat shape. | P2 | Not started |
| MS-EDIT-04 | Emoji | Add emoji annotations. | P2 | Not started |
| MS-EDIT-05 | Eraser | Remove added annotation strokes/shapes. | P0 | Not started |
| MS-EDIT-06 | Crop | Crop the captured image. | P0 | Not started |
| MS-EDIT-07 | Undo/redo | Undo and redo annotation/edit actions. | P0 | Not started |
| MS-EDIT-08 | Quick markup | Enable markup before finalising the capture; annotate in the selection workflow. | P1 | Not started |
| MS-EDIT-09 | Re-crop handles | Adjust the selected image bounds using handles in Quick markup. | P1 | Not started |

### 2.3 Text actions / OCR — confirmed in Microsoft Support and Microsoft feature announcements

| ID | Feature | Documented behaviour | Priority | Status |
|---|---|---|---|---|
| MS-TEXT-01 | Text actions / OCR | Recognise text in a captured image locally on the device. | P1 | Not started |
| MS-TEXT-02 | Select recognised text | Select a portion of recognised text for copying. | P1 | Not started |
| MS-TEXT-03 | Copy all text | Copy all recognised text from the capture. | P1 | Not started |
| MS-TEXT-04 | Quick redact | Detect and redact email addresses and phone numbers. | P2 | Not started |
| MS-TEXT-05 | Text Extractor capture mode | Select a screen region to extract text without retaining a screenshot as the primary result. | P2 | Not started |
| MS-TEXT-06 | Remove line breaks | Option to remove line breaks from extracted text. | P2 | Not started |
| MS-TEXT-07 | Automatic text copy | Option to automatically copy extracted text and dismiss the selection flow. | P2 | Not started |

### 2.4 Quick markup actions and sharing — announced by Microsoft

| ID | Feature | Documented behaviour | Priority | Status |
|---|---|---|---|---|
| MS-QUICK-01 | Quick markup toggle | Toggle Quick markup before selecting the capture region; Microsoft documents `Ctrl + E`. | P1 | Not started |
| MS-QUICK-02 | Quick markup pen/highlighter/eraser | Use the classic markup tools during the capture selection flow. | P1 | Not started |
| MS-QUICK-03 | Quick markup shapes/emoji | Add shapes and emoji before finalising the snip. | P2 | Not started |
| MS-QUICK-04 | Quick markup share action | Provide the documented share action from the Quick markup workflow. | P2 | Not started |
| MS-QUICK-05 | Visual Search with Bing action | Provide an optional hand-off action for the captured image. | P2 | Not started |
| MS-QUICK-06 | Ask Copilot action | Provide an optional hand-off action for the captured image. | P3 | Not started |

**Behaviour note:** Microsoft's September 2025 Quick markup announcement says that using its Share, Visual Search with Bing or Ask Copilot actions does not copy the image to the clipboard or autosave it. Our implementation should define and test its own predictable save/copy behaviour rather than accidentally discard a capture.

### 2.5 Hardware-/rollout-dependent capture utilities

| ID | Feature | Documented behaviour / condition | Priority | Status |
|---|---|---|---|---|
| MS-AI-01 | Perfect screenshot | AI-assisted rectangle selection that adjusts bounds to visible content and allows handle refinement. Microsoft documents this as Copilot+ PC-only. | P3 | Not started |
| MS-AI-02 | Color picker | Inspect/copy screen colours. Microsoft sources have differed on hardware availability: the current Support article labels it Copilot+ PC-only, while a 2025 Windows Experience announcement described colour picking for all Windows 11 PCs. Treat availability as **to be verified on the target OS/app/device**, not guaranteed. | P2 | Not started |
| MS-AI-03 | Colour output formats | Microsoft announced HEX, RGB and HSL values. | P2 | Not started |
| MS-AI-04 | Color picker zoom | Zoom around the pointer/colour selection, including documented Ctrl +/- controls. | P3 | Not started |

**Implementation rule:** Capability detection and graceful fallback are required. Do not make the base application depend on Copilot+ hardware or an AI service.

### 2.6 Screen recording — confirmed in Microsoft Support and feature announcements

| ID | Feature | Documented behaviour | Priority | Status |
|---|---|---|---|---|
| MS-REC-01 | Video snip mode | Switch the capture toolbar to recording mode. | P1 | Not started |
| MS-REC-02 | Rectangular recording region | Select/refine a screen region before recording. | P1 | Not started |
| MS-REC-03 | Start/stop recording | Start and stop a recording session. | P1 | Not started |
| MS-REC-04 | Pause/resume recording | Pause/resume is documented in Microsoft's screen recording guidance/history. | P2 | Not started |
| MS-REC-05 | Microphone audio | Optionally capture voice-over from a microphone. | P2 | Not started |
| MS-REC-06 | PC/system audio | Optionally capture PC audio. | P2 | Not started |
| MS-REC-07 | Audio preferences | Configure default audio options and select an input device when multiple recording devices exist. | P2 | Not started |
| MS-REC-08 | Window recording | Choose a specific app window as the recording area. The 2025 announcement says the region remains fixed after recording starts; it does not follow a moved/covered window. | P2 | Not started |
| MS-REC-09 | Recording preview | Preview a recording before saving/sharing. | P1 | Not started |
| MS-REC-10 | Recording auto-save | Automatically save recordings to a Screen Recordings folder, with a setting to turn it off. | P1 | Not started |
| MS-REC-11 | Trim recording | Adjust the start/end of a recording; apply/cancel and revisit trimming. | P2 | Not started |
| MS-REC-12 | GIF export | Export a recording as GIF, choose low/high quality, save or copy. Microsoft documented a 30-second limit for the feature announcement. | P2 | Not started |
| MS-REC-13 | Open in Clipchamp | Hand off a recording to Clipchamp for further editing. | P3 | Not started |
| MS-REC-14 | Captions/audio in Clipchamp | The Microsoft workflow uses Clipchamp to generate captions and adjust/detach audio. This is an external-app integration, not an in-app editor requirement for our first release. | P3 | Not started |

### 2.7 Save, copy, share and common shortcuts — confirmed in Microsoft Support

| ID | Feature | Documented behaviour | Priority | Status |
|---|---|---|---|---|
| MS-OUT-01 | Copy capture | Copy a still snip to the clipboard. | P0 | Not started |
| MS-OUT-02 | Save as | Choose a filename and destination for an additional copy. | P0 | Not started |
| MS-OUT-03 | Automatic screenshot saving | Save captures automatically to a Screenshots folder. | P0 | Not started |
| MS-OUT-04 | Auto-save settings | Let users change automatic-save behaviour and the default Screenshots folder. | P1 | Not started |
| MS-OUT-05 | Share | Share a captured image through Windows sharing/application hand-off. | P1 | Not started |
| MS-OUT-06 | Print | Provide a print workflow for captured images. | P2 | Not started |
| MS-OUT-07 | Open/edit with Paint | Provide an optional hand-off to Paint for additional image editing. | P3 | Not started |
| MS-OUT-08 | Video auto-save settings | Let users control auto-save behaviour for screen recordings. | P2 | Not started |

### 2.8 Documented shortcut baseline

These are shortcuts listed by Microsoft's current Support article or feature announcements. We should match their purpose where feasible and document any conflict with Windows-level shortcut ownership.

| Shortcut | Purpose in Microsoft Snipping Tool | Our status |
|---|---|---|
| `Win + Shift + S` | Open image snipping overlay | Not started |
| `Win + Shift + R` | Open video snipping overlay | Not started |
| `PrtSc` | Invoke the configured Windows screenshot action / Snipping Tool entry point | Not started |
| `Alt + M` | Choose snipping mode | Not started |
| `Alt + N` | Create a new snip in the last mode | Not started |
| `Shift + Arrow keys` | Keyboard navigation/movement during snip selection (match actual user-visible behaviour during testing) | Not started |
| `Alt + D` | Set capture delay (1–5 seconds) | Not started |
| `Ctrl + C` | Copy the snip | Not started |
| `Ctrl + S` | Save the snip | Not started |
| `Ctrl + E` | Toggle Quick markup | Not started |
| `Ctrl + +/-` | Color picker zoom controls, where supported | Not started |

**Shortcut design constraint:** Windows reserves or user-configures some system shortcuts. The app must never assume it can register every shortcut globally. Provide a settings page, collision detection, and an in-app fallback.

---

## 3. Our additional Scroll Capture feature

This is the product's primary differentiator. It is **not** a built-in feature listed in Microsoft's documented Snipping Tool feature set above; it is our own feature set.

### 3.1 Scroll capture MVP

| ID | Feature | Acceptance criteria | Priority | Status |
|---|---|---|---|---|
| SCROLL-01 | Scroll mode in capture toolbar | A clearly labelled Scroll mode is available alongside standard capture modes. | P0 | Not started |
| SCROLL-02 | Select scroll region | User can select a rectangular region/window to be extended vertically. Coordinates and dimensions are DPI/multi-monitor aware. | P0 | Not started |
| SCROLL-03 | Capture initial frame | First viewport is captured exactly once and displayed as frame 1. | P0 | Not started |
| SCROLL-04 | Capture More action | User advances the target content by one controlled step, waits for it to settle, and captures the next viewport. | P0 | Not started |
| SCROLL-05 | Finish & Save action | User ends capture and gets one stitched output image. | P0 | Not started |
| SCROLL-06 | Cancel action | User can abandon the session without saving a final image. | P0 | Not started |
| SCROLL-07 | Live preview | Preview updates after each accepted frame. | P1 | Not started |
| SCROLL-08 | Overlap detection | Align adjacent frames by matching image content; reject low-confidence alignment with a useful error. | P0 | Not started |
| SCROLL-09 | Seam handling | Avoid duplicated rows, gaps, black seams and visible discontinuities. | P0 | Not started |
| SCROLL-10 | Long-image limits | Enforce documented dimensions/memory limits and explain them to the user. | P0 | Not started |
| SCROLL-11 | Export | Save the stitched result as a PNG in the app's configured output location. | P0 | Not started |
| SCROLL-12 | Frame count/status | Show progress, frame count, capture-in-progress state and errors. | P1 | Not started |

### 3.2 Scroll capture robustness and advanced features

| ID | Feature | Acceptance criteria | Priority | Status |
|---|---|---|---|---|
| SCROLL-13 | Sticky/fixed UI handling | Detect or let user exclude fixed headers, footers, chat composers and overlays; don't silently repeat fixed UI in the output. | P1 | Not started |
| SCROLL-14 | Manual scroll-step tuning | Offer a safe step size and settling delay; do not assume every app scrolls the same amount. | P1 | Not started |
| SCROLL-15 | Auto-scroll mode | Optional automated incremental scrolling with stop/continue controls. | P2 | Not started |
| SCROLL-16 | Scroll boundary detection | Detect end-of-content and avoid appending repeated final frames. | P2 | Not started |
| SCROLL-17 | Horizontal scrolling | Support horizontal or two-dimensional content only after vertical capture is reliable. | P3 | Not started |
| SCROLL-18 | Undo last frame | Remove the most recent frame and allow recapture. | P2 | Not started |
| SCROLL-19 | Frame review | Let users inspect, discard or retry frames before final export. | P2 | Not started |
| SCROLL-20 | Export long capture to PDF | Optional paginated PDF export in addition to the continuous PNG. | P3 | Not started |
| SCROLL-21 | Scroll capture for window | Allow selecting an app window and keep capture coordinates associated with that window where technically possible. | P2 | Not started |
| SCROLL-22 | Accessibility | Expose controls/status to UI Automation and support keyboard operation. | P1 | Not started |

### 3.3 Scroll capture known risks to validate

- Some apps use virtualized lists, canvas rendering, lazy-loaded content or custom scroll surfaces; image overlap matching may not be enough for every app.
- Fixed/sticky headers, footers, chat composers and browser overlays can be captured in every viewport. The UI must let users exclude or account for these instead of applying an unsafe global crop.
- Content can move or reflow while capture is running. We need a settle delay and alignment confidence checks.
- Protected video/DRM content, elevated/secure desktops and some hardware-accelerated surfaces may not be capturable in the same way as ordinary desktop content. Fail clearly rather than claiming universal support.
- Multi-monitor and mixed-DPI geometry must be tested before calling capture production-ready.

---

## 4. Architecture and technical decisions

### 4.1 Proposed subsystem boundaries

| Subsystem | Responsibility | Proposed implementation direction |
|---|---|---|
| App shell | Startup/shutdown, window lifecycle, single-instance policy, settings bootstrap | C++20, Windows desktop app |
| UI shell | Floating toolbar, menus, settings, editor window | Evaluate WinUI 3 (C++/WinRT) versus Win32 + Direct2D/DirectWrite in a short technical spike; choose one before building the full UI |
| Capture selection | Rectangle/freeform overlays, window picker, display picker, DPI-aware coordinates | Win32 window/input handling plus Windows.Graphics.Capture where appropriate |
| Frame acquisition | Still frames and recording frames | Windows.Graphics.Capture + Direct3D 11; validate platform and packaging requirements |
| Image pipeline | Pixel conversion, crop, alpha handling, scale, colour formats | WIC + Direct2D/Direct3D as appropriate |
| Editor model | Non-destructive annotations, crop, undo/redo | Separate document/model layer from UI |
| OCR | Text recognition, selection, copy and redact | Windows OCR/WinRT capability investigation; make language availability explicit |
| Color picker | Screen sampling, zoom, HEX/RGB/HSL output | Pixel sampler and magnifier overlay |
| Recording | Video frames, microphone/system audio, pause/resume, trim | Windows.Graphics.Capture + Media Foundation/appropriate Windows media APIs; technical spike required |
| Scrolling | Region capture, scroll driver, overlap matching, stitch/export | Independent C++ subsystem; no UI code inside the stitcher |
| Storage | Screenshot/recording directories, filenames, recent items, preferences | Filesystem + versioned settings format |
| Packaging | App identity, protocol/shortcut integration as needed, MSIX | MSIX with repeatable build/sign/test pipeline |

### 4.2 Architecture rules

1. Keep capture engines independent from UI widgets.
2. Keep the screenshot editor non-destructive: original pixels plus editable annotation/crop state until export.
3. Keep scroll stitching a deterministic library with testable inputs/outputs; do not put its logic in window procedures.
4. Use RAII for Win32, COM, WinRT, Direct3D and GDI resources.
5. Every capture mode returns a common capture result/document model so still capture, OCR, editing and export share the same pipeline.
6. Make DPI, monitor origin, display scaling and colour format explicit in types; avoid assuming a single 1920x1080 display or origin `(0,0)`.
7. Never overwrite originals unexpectedly. Autosave, explicit Save, Copy and export actions must have documented behaviour.
8. The app must remain usable when optional features (OCR language pack, Copilot+-only capability, audio device, capture item) are unavailable.
9. Keep the UI responsive: expensive image work, OCR, stitching and recording encode run off the UI thread with cancellation/progress.
10. Add automated tests for geometry, image operations, overlap scoring, PNG round-trips and settings serialization before adding more UI.

### 4.3 Technical decision gates — do not assume these are settled

| Decision | Required experiment | Exit criterion | Status |
|---|---|---|---|
| UI framework | Build a compact toolbar, dropdown, rounded surfaces, editor window and accessibility sample in candidate UI stacks. | Choose the stack that best matches the target UI and is maintainable with the installed Visual Studio/CMake toolchain. | Not started |
| Minimum Windows version | Identify exact Windows build/API availability for capture, media, OCR and packaging APIs. | Publish a tested minimum OS version and runtime capability table. | Not started |
| Capture API | Capture a display and a chosen window with cursor/border behaviour controlled. | Stable test on target Windows build and multi-monitor setup. | Not started |
| Recorder API | Capture video plus optional system audio/microphone. | Reliable 30-second test, pause/resume, audio sync and resource cleanup. | Not started |
| OCR API | Extract text from test images in required languages. | Accurate local extraction with clear language-pack failure behaviour. | Not started |
| Global shortcut | Test registration, conflicts and Windows ownership. | User-configurable shortcut with a documented fallback; no forced takeover. | Not started |

---

## 5. Milestone plan

Milestones are ordered by dependency. Dates are deliberately not estimated yet; we will estimate after the first technical spike and a working vertical slice. **A milestone is complete only when its exit criteria pass.**

### M0 — Requirements and repository baseline

**Goal:** Turn this roadmap into the single source of truth and verify the development environment.

- [ ] M0.1 Save this roadmap in `docs/DEVELOPMENT_ROADMAP.md`.
- [ ] M0.2 Record the exact Windows edition, version, build, architecture and display/DPI configuration.
- [ ] M0.3 Record Visual Studio/MSVC, Windows SDK, CMake and Git versions.
- [ ] M0.4 Choose the UI framework via a small prototype; document why.
- [ ] M0.5 Confirm the minimum Windows version and package model.
- [ ] M0.6 Create a clean CMake build and a minimal app that opens/closes cleanly.

**Exit criteria:** The project builds from a clean configure; the app launches on the target PC; UI, capture, storage and packaging decisions are recorded.

### M1 — Application foundation

- [ ] M1.1 Define application entry point and lifecycle.
- [ ] M1.2 Implement single-instance behaviour or a documented multi-instance policy.
- [ ] M1.3 Add DPI awareness and monitor geometry service.
- [ ] M1.4 Add structured logging and error reporting.
- [ ] M1.5 Add settings model and safe default directories.
- [ ] M1.6 Add build/test configuration and warnings-as-errors policy where practical.
- [ ] M1.7 Add app icon, version metadata and initial MSIX identity plan.

**Exit criteria:** App starts/stops cleanly, logs diagnostics, reads/writes versioned settings, and behaves correctly at 100%, 125%, 150% and 200% scale on the test display.

### M2 — Capture toolbar and overlay

- [ ] M2.1 Recreate the compact Snipping Tool-style floating toolbar.
- [ ] M2.2 Image/video mode toggle.
- [ ] M2.3 Capture-mode dropdown: Rectangle, Window, Full screen, Freeform, Scroll.
- [ ] M2.4 Delay selector (1–5 seconds).
- [ ] M2.5 Close/cancel behaviour and Escape handling.
- [ ] M2.6 Rectangle overlay with visual feedback and keyboard support.
- [ ] M2.7 Freeform overlay.
- [ ] M2.8 Window selection.
- [ ] M2.9 Full display selection, including multi-monitor.
- [ ] M2.10 Shortcut configuration and conflict detection.

**Exit criteria:** All still modes return correct selection geometry and do not leave overlays/input hooks active after cancel/failure.

### M3 — Still capture engine and output

- [ ] M3.1 Implement display capture.
- [ ] M3.2 Implement window capture.
- [ ] M3.3 Implement selected-rectangle capture.
- [ ] M3.4 Implement freeform mask/capture output.
- [ ] M3.5 Handle cursor inclusion/exclusion as a capture option where supported.
- [ ] M3.6 Support multiple monitors and negative virtual-screen coordinates.
- [ ] M3.7 Create common capture document/result model.
- [ ] M3.8 Copy to clipboard.
- [ ] M3.9 Save as and default auto-save.
- [ ] M3.10 Implement output naming, collision handling and configurable folders.
- [ ] M3.11 Implement common share/print hand-off where practical.

**Exit criteria:** Each capture mode produces correct images on the test matrix; clipboard and saved output match; cancel/error paths do not corrupt user data.

### M4 — Scroll capture MVP (our differentiator)

- [ ] M4.1 Add Scroll to capture-mode dropdown.
- [ ] M4.2 Select the scrolling region/window.
- [ ] M4.3 Capture initial frame exactly once.
- [ ] M4.4 Add Capture More / Finish & Save / Cancel controller.
- [ ] M4.5 Implement one-step scroll input and settling delay.
- [ ] M4.6 Implement overlap detection and confidence score.
- [ ] M4.7 Implement stitch composition without seams/gaps/repeated content.
- [ ] M4.8 Add live preview and frame count.
- [ ] M4.9 Add safe output dimension/memory limits.
- [ ] M4.10 Add fixed/sticky UI exclusion workflow.
- [ ] M4.11 Add error recovery when alignment fails.

**Exit criteria:** A long browser page, document and text-heavy page can be captured repeatedly with no visible seams or duplicated fixed UI, and outputs are saved reproducibly.

### M5 — Screenshot editor and Quick markup

- [ ] M5.1 Editor window and image canvas.
- [ ] M5.2 Pen with colour/size controls.
- [ ] M5.3 Highlighter with colour/size controls.
- [ ] M5.4 Eraser.
- [ ] M5.5 Shapes: line, arrow, rectangle, ellipse and other selected shapes.
- [ ] M5.6 Emoji annotations.
- [ ] M5.7 Crop and re-crop.
- [ ] M5.8 Undo/redo command history.
- [ ] M5.9 Quick markup before final capture confirmation.
- [ ] M5.10 Clipboard copy and save from editor.

**Exit criteria:** Editing is non-destructive until export; undo/redo is deterministic; saved output matches the canvas; selection/crop handles work at all tested DPIs.

### M6 — OCR, text actions and colour tools

- [ ] M6.1 Investigate Windows OCR API and language availability.
- [ ] M6.2 Add local OCR to captured images.
- [ ] M6.3 Select recognised text and copy selection.
- [ ] M6.4 Copy all text.
- [ ] M6.5 Remove line breaks option.
- [ ] M6.6 Automatic text extraction/copy mode.
- [ ] M6.7 Quick redact emails and phone numbers.
- [ ] M6.8 Text Extractor capture mode.
- [ ] M6.9 Color picker overlay and magnifier.
- [ ] M6.10 HEX/RGB/HSL output.
- [ ] M6.11 Perfect screenshot capability spike; gate by device/API capability.

**Exit criteria:** OCR works locally on a documented set of languages/images; redact preview is editable before applying; optional AI/hardware utilities fail gracefully when unavailable.

### M7 — Screen recording

- [ ] M7.1 Recording mode in the capture toolbar.
- [ ] M7.2 Rectangle recording region.
- [ ] M7.3 Window recording selection.
- [ ] M7.4 Start/stop/pause/resume.
- [ ] M7.5 Microphone capture option.
- [ ] M7.6 System/PC audio capture option.
- [ ] M7.7 Audio device selection and default settings.
- [ ] M7.8 Recording preview.
- [ ] M7.9 Automatic recording save and folder settings.
- [ ] M7.10 Trim start/end.
- [ ] M7.11 GIF export with quality selection and duration policy.
- [ ] M7.12 Optional Clipchamp hand-off.

**Exit criteria:** A test recording is smooth, audio is synchronised, pause/resume works, trim output is correct and export/cancel paths release capture resources.

### M8 — Settings, recents and workflow polish

- [ ] M8.1 Settings window.
- [ ] M8.2 Default save folders and auto-save toggles.
- [ ] M8.3 Capture defaults and delay preferences.
- [ ] M8.4 Recording audio preferences.
- [ ] M8.5 User-configurable shortcuts with collision detection.
- [ ] M8.6 Recent captures/history view (our product addition; not assumed as a parity item unless verified on the reference build).
- [ ] M8.7 Reopen, rename, delete and reveal-in-folder actions for our history items.
- [ ] M8.8 Optional startup/tray behaviour.
- [ ] M8.9 Keyboard navigation, high contrast, screen-reader labels and touch/pen checks.

**Exit criteria:** Settings persist across restarts; recents never point to deleted files; controls are accessible and keyboard-operable.

### M9 — Reliability, tests and performance

- [ ] M9.1 Unit tests for geometry, crop, pixel conversion and file naming.
- [ ] M9.2 Unit tests for scrolling overlap detection and stitch output.
- [ ] M9.3 Image regression tests using known fixtures.
- [ ] M9.4 Multi-monitor/mixed-DPI test matrix.
- [ ] M9.5 Long scrolling-session memory test.
- [ ] M9.6 Recording duration/audio-sync test.
- [ ] M9.7 High-resolution and large-image tests.
- [ ] M9.8 Failure-injection tests for capture cancellation/device loss.
- [ ] M9.9 Verify no GDI/COM/WinRT/D3D resource leaks.
- [ ] M9.10 Measure cold start, capture latency, preview responsiveness and memory use.

**Exit criteria:** No known data-loss bug; no critical leaks; documented performance targets achieved on the target device; all release-blocking tests pass.

### M10 — Packaging and release

- [ ] M10.1 Final app identity, display name, icons and version.
- [ ] M10.2 MSIX packaging pipeline.
- [ ] M10.3 Signing and signature verification.
- [ ] M10.4 Install/upgrade/uninstall testing.
- [ ] M10.5 Startup/shortcut registration only with explicit user consent.
- [ ] M10.6 User-facing privacy and capture-indicator behaviour.
- [ ] M10.7 Release notes and known limitations.
- [ ] M10.8 Clean-machine installation test.
- [ ] M10.9 Tag release commit and archive build artefacts.

**Exit criteria:** Signed package installs/upgrades cleanly on the declared minimum Windows version, retains user settings on upgrade, and uninstalls without leaving app-owned temporary data unexpectedly.

---

## 6. Definition of Done

A roadmap item can be marked `[x]` only when all relevant points are true:

- [ ] Code is implemented in the correct subsystem and reviewed.
- [ ] The project builds from a clean CMake/MSBuild configure/build.
- [ ] Acceptance criteria are demonstrated on the target Windows installation.
- [ ] Failure/cancel paths are tested.
- [ ] DPI/multi-monitor behaviour is tested if screen coordinates or windows are involved.
- [ ] No known data-loss, capture-corruption or resource-leak issue remains.
- [ ] Documentation and feature status are updated.
- [ ] The change is committed to Git with a meaningful message.

A successful compile alone does **not** make a feature Done.

---

## 7. Test matrix

Record the actual result and hardware/software details in `docs/TEST_LOG.md` as tests are performed.

| Test area | Required cases | Status |
|---|---|---|
| Windows environment | Current target OS build; declared minimum OS build | Not tested |
| Display topology | Single monitor; multiple monitors; primary monitor not at `(0,0)` | Not tested |
| DPI | 100%, 125%, 150%, 200%; mixed DPI across monitors | Not tested |
| Capture modes | Rectangle; freeform; window; full screen | Not tested |
| Display content | Browser; text-heavy page; image-heavy page; document; menus/tooltips | Not tested |
| Window behaviour | Minimise/restore; resize; move; occlusion; elevated window | Not tested |
| Scroll content | Static page; dynamic/lazy-loaded page; sticky header/footer; long document; chat-like fixed composer | Not tested |
| Scroll failure | No movement; content reflow; ambiguous overlap; reached page bottom; cancelled mid-session | Not tested |
| Editor | Pen/highlighter/shape/emoji/crop/eraser/undo/redo | Not tested |
| OCR | High-contrast text; small text; multi-column; mixed languages; image-only text | Not tested |
| Recording | Region/window; pause/resume; microphone; system audio; no audio device; long session | Not tested |
| Storage | Save; overwrite collision; invalid path; disk full; auto-save off; reopen history | Not tested |
| Packaging | Fresh install; upgrade; downgrade rejection; uninstall; app-data persistence | Not tested |
| Accessibility | Keyboard only; screen reader; high contrast; touch/pen if available | Not tested |

---

## 8. Tracker dashboard

Update this dashboard whenever a milestone changes. Keep the status values consistent.

| Milestone | Name | Status | Notes |
|---|---|---|---|
| M0 | Requirements and repository baseline | Not started | Current task: establish project baseline and choose UI framework. |
| M1 | Application foundation | Not started | |
| M2 | Capture toolbar and overlay | Not started | |
| M3 | Still capture engine and output | Not started | |
| M4 | Scroll capture MVP | Not started | Product differentiator. |
| M5 | Screenshot editor and Quick markup | Not started | |
| M6 | OCR, text actions and colour tools | Not started | Includes gated/optional capabilities. |
| M7 | Screen recording | Not started | Includes audio, trim and GIF export. |
| M8 | Settings, recents and workflow polish | Not started | |
| M9 | Reliability, tests and performance | Not started | |
| M10 | Packaging and release | Not started | |

### Status vocabulary

- **Not started** — no implementation work has begun.
- **In progress** — work is underway; acceptance criteria are not all met.
- **Blocked** — waiting on a technical decision, API, toolchain or dependency.
- **Testing** — implementation is complete enough for acceptance testing.
- **Done** — acceptance criteria and Definition of Done have passed.
- **Deferred** — intentionally moved out of the current release scope; retain the item for visibility.

### Change log

| Date | Change |
|---|---|
| 2026-11-15 | Created greenfield roadmap and verified baseline against Microsoft Support and Windows Insider/Experience announcements. |

---

## 9. Source register — official Microsoft sources

This register supports the **reference feature inventory**, not a claim that every feature is present on every Windows installation. Product rollout and eligibility can vary. Re-check sources and test the user's installed Windows build before implementing hardware-gated behaviour.

1. **Microsoft Support — Use Snipping Tool to capture screenshots** (current support article; capture modes, video snips, shortcuts, delay, editor tools, OCR/text actions, save/share/print and Copilot+ notes):\
   https://support.microsoft.com/en-us/windows/apps/use-snipping-tool-to-capture-screenshots
2. **Windows Insider Blog — Text extractor in Snipping Tool begins rolling out** (15 April 2025; text extraction, copy all, remove line breaks and automatic copy options):\
   https://blogs.windows.com/windows-insider/2025/04/15/text-extractor-in-snipping-tool-begins-rolling-out-to-windows-insiders/
3. **Windows Insider Blog — Snipping Tool and Notepad app updates** (13 March 2025; draw-and-hold converts pen strokes into neat lines and shapes):\
   https://blogs.windows.com/windows-insider/2025/03/13/snipping-tool-and-notepad-app-updates-begin-rolling-out-to-windows-insiders/
4. **Windows Insider Blog — Paint, Snipping Tool, and Notepad updates** (22 May 2025; Perfect screenshot and Color picker; Copilot+ context):\
   https://blogs.windows.com/windows-insider/2025/05/22/paint-snipping-tool-and-notepad-updates-with-new-features-begin-rolling-out-to-windows-insiders/
5. **Windows Experience Blog — Windows 11 AI experiences** (22 July 2025; colour values HEX/RGB/HSL, zoom and colour-picker description):\
   https://blogs.windows.com/windowsexperience/2025/07/22/windows-11-is-the-home-for-ai-on-the-pc-with-even-more-experiences-available-today/
6. **Windows Insider Blog — Screen recording trim** (21 February 2025; recording trim workflow):\
   https://blogs.windows.com/windows-insider/2025/02/21/announcing-windows-11-insider-preview-build-26120-3291-dev-and-beta-channels/
7. **Windows Insider Blog — GIF export in Snipping Tool** (19 June 2025; GIF quality choice, save/copy and 30-second limit):\
   https://blogs.windows.com/windows-insider/2025/06/19/gif-export-in-snipping-tool-begins-rolling-out-to-windows-insiders/
8. **Windows Insider Blog — Window-mode screen recording** (14 August 2025; choose an application window for recording; fixed region after start):\
   https://blogs.windows.com/windows-insider/2025/08/14/announcing-windows-11-insider-preview-build-27924-canary-channel/
9. **Windows Insider Blog — Quick markup** (17 September 2025; `Ctrl + E`, pre-finalisation markup, tools, crop handles, Share/Visual Search/Copilot hand-offs):\
   https://blogs.windows.com/windows-insider/2025/09/17/paint-snipping-tool-and-notepad-app-updates-begin-rolling-out-to-windows-insiders/
10. **Windows Insider Blog — Screen recording audio and microphone** (31 August 2023; PC audio, microphone, audio defaults and device selection):\
    https://blogs.windows.com/windows-insider/2023/08/31/new-updates-for-snipping-tool-and-notepad-for-windows-insiders/
11. **Microsoft Learn — Screen capture** (Windows.Graphics.Capture API overview; display/window frame acquisition and system picker):\
    https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture
12. **Microsoft Windows Experience Blog — What’s new in Windows 11** (15 October 2026; contemporary Windows performance context; does not announce additional Snipping Tool feature functionality):\
    https://blogs.windows.com/windowsexperience/2026/10/15/whats-new-in-windows-11-everyday-experiences-reimagined/

### Evidence notes and unresolved points

- The Microsoft Support article is the primary current feature inventory used for this roadmap.
- Microsoft feature announcements describe when capabilities began rolling out; announcement does not prove every build/device has the feature enabled.
- Microsoft's current Support article labels Perfect screenshot and Color picker as Copilot+ PC features. A July 2025 Windows Experience announcement described Color picker as available on all Windows 11 PCs. This roadmap deliberately flags Color picker eligibility as conflicting/rollout-dependent and requires a real-device check.
- Microsoft documentation describes Snipping Tool's user-visible behaviour, not its private implementation. The implementation architecture in Section 4 is our engineering proposal and must be validated with prototypes.
- A feature not listed in the public sources above is not assumed to exist. If the user finds another feature in their installed Snipping Tool, add it to the inventory only after recording the app version/build and a reproducible observation.

---

## 10. Immediate next actions

Do these in order; keep the scope small until M0 is complete:

1. Save this file as `docs/DEVELOPMENT_ROADMAP.md` in the cleaned repository.
2. Create `docs/TEST_LOG.md` with the actual Windows build, app/toolchain versions and the test matrix headings.
3. Run a platform/toolchain inventory and record exact versions. Do not guess the Windows version from the product name alone; record `winver` and `Get-ComputerInfo` output.
4. Build a UI-framework spike: same small floating toolbar, dropdown, dark/light surfaces and one overlay in each candidate framework.
5. Decide the UI framework, app packaging model and minimum OS version before building the full capture engine.
6. Start M1 only after those decisions are recorded.

**Current implementation status: 0 of 11 milestones complete.** The repository was cleaned for the restart; no new application code is included in this roadmap document.
