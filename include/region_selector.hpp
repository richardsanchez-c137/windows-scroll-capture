#pragma once

#include <windows.h>

// Displays a full-screen selection overlay.
// Returns false when the user presses Escape.
bool selectCaptureRegion(RECT& selectedRegion);
