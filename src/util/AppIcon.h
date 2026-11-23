#pragma once

#include <windows.h>

namespace rm {

// Loads large/small icons from the executable's embedded IDI_REGION_MIRROR resource.
// Returns false if either load fails.
bool LoadAppIcons(HINSTANCE instance, HICON* largeIcon, HICON* smallIcon);

// Apply icons to a top-level window (title bar / taskbar / Alt+Tab).
void ApplyWindowIcons(HWND hwnd, HICON largeIcon, HICON smallIcon);

} // namespace rm
