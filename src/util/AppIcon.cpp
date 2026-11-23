#include "pch.h"
#include "AppIcon.h"
#include "resource.h"

namespace rm {

bool LoadAppIcons(HINSTANCE instance, HICON* largeIcon, HICON* smallIcon)
{
    if (!instance || !largeIcon || !smallIcon) {
        return false;
    }

    const int cxLarge = GetSystemMetrics(SM_CXICON);
    const int cyLarge = GetSystemMetrics(SM_CYICON);
    const int cxSmall = GetSystemMetrics(SM_CXSMICON);
    const int cySmall = GetSystemMetrics(SM_CYSMICON);

    HICON loadedLarge = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_REGION_MIRROR),
        IMAGE_ICON,
        cxLarge,
        cyLarge,
        0));
    HICON loadedSmall = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_REGION_MIRROR),
        IMAGE_ICON,
        cxSmall,
        cySmall,
        0));

    if (!loadedLarge || !loadedSmall) {
        if (loadedLarge) {
            DestroyIcon(loadedLarge);
        }
        if (loadedSmall) {
            DestroyIcon(loadedSmall);
        }
        // Fallback: LoadIcon picks a default size from the resource.
        loadedLarge = LoadIconW(instance, MAKEINTRESOURCEW(IDI_REGION_MIRROR));
        loadedSmall = loadedLarge;
        if (!loadedLarge) {
            return false;
        }
    }

    *largeIcon = loadedLarge;
    *smallIcon = loadedSmall;
    return true;
}

void ApplyWindowIcons(HWND hwnd, HICON largeIcon, HICON smallIcon)
{
    if (!hwnd) {
        return;
    }
    if (largeIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    }
    if (smallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
}

} // namespace rm
