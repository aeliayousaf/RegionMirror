#include "pch.h"
#include "DisplayEnum.h"

#include <ShellScalingApi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shcore.lib")

namespace rm {
namespace {

std::wstring QueryFriendlyName(const DISPLAYCONFIG_PATH_INFO& path)
{
    DISPLAYCONFIG_TARGET_DEVICE_NAME nameInfo{};
    nameInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    nameInfo.header.size = sizeof(nameInfo);
    nameInfo.header.adapterId = path.targetInfo.adapterId;
    nameInfo.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&nameInfo.header) != ERROR_SUCCESS) {
        return {};
    }
    if (nameInfo.flags.friendlyNameFromEdid) {
        return nameInfo.monitorFriendlyDeviceName;
    }
    return nameInfo.monitorDevicePath;
}

bool MatchPathToDevice(const DISPLAYCONFIG_PATH_INFO& path, const std::wstring& gdiDeviceName)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
    sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size = sizeof(sourceName);
    sourceName.header.adapterId = path.sourceInfo.adapterId;
    sourceName.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
        return false;
    }
    return _wcsicmp(sourceName.viewGdiDeviceName, gdiDeviceName.c_str()) == 0;
}

std::wstring LookupFriendlyName(const std::wstring& gdiDeviceName)
{
    UINT pathCount = 0;
    UINT modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return {};
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return {};
    }
    paths.resize(pathCount);
    for (const auto& path : paths) {
        if (MatchPathToDevice(path, gdiDeviceName)) {
            return QueryFriendlyName(path);
        }
    }
    return {};
}

BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
{
    auto* out = reinterpret_cast<std::vector<DisplayInfo>*>(lParam);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return TRUE;
    }

    DisplayInfo info;
    info.monitor = monitor;
    info.deviceName = mi.szDevice;
    info.bounds = mi.rcMonitor;
    info.workArea = mi.rcWork;
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    info.friendlyName = LookupFriendlyName(info.deviceName);

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        info.dpi = dpiX;
    }

    out->push_back(std::move(info));
    return TRUE;
}

} // namespace

std::wstring DisplayInfo::label() const
{
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    std::wstring name = friendlyName.empty() ? deviceName : friendlyName;
    wchar_t buf[512];
    swprintf_s(buf, L"%s — %dx%d @ (%d,%d)%s",
               name.c_str(), w, h, bounds.left, bounds.top,
               primary ? L" [Primary]" : L"");
    return buf;
}

std::wstring DisplayInfo::preferenceId() const
{
    // Prefer stable device name; fall back to geometry key.
    if (!deviceName.empty()) {
        return deviceName;
    }
    wchar_t buf[128];
    swprintf_s(buf, L"geom:%d,%d,%d,%d", bounds.left, bounds.top, bounds.right, bounds.bottom);
    return buf;
}

std::vector<DisplayInfo> EnumerateDisplays()
{
    std::vector<DisplayInfo> displays;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&displays));
    std::stable_sort(displays.begin(), displays.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
        if (a.primary != b.primary) {
            return a.primary && !b.primary;
        }
        if (a.bounds.left != b.bounds.left) {
            return a.bounds.left < b.bounds.left;
        }
        return a.bounds.top < b.bounds.top;
    });
    return displays;
}

const DisplayInfo* FindDisplayByPreferenceId(const std::vector<DisplayInfo>& displays, const std::wstring& id)
{
    if (id.empty()) {
        return nullptr;
    }
    for (const auto& d : displays) {
        if (d.preferenceId() == id) {
            return &d;
        }
    }
    return nullptr;
}

const DisplayInfo* FindDisplayByMonitor(const std::vector<DisplayInfo>& displays, HMONITOR monitor)
{
    for (const auto& d : displays) {
        if (d.monitor == monitor) {
            return &d;
        }
    }
    return nullptr;
}

const DisplayInfo* DefaultOutputDisplay(const std::vector<DisplayInfo>& displays)
{
    if (displays.empty()) {
        return nullptr;
    }
    for (const auto& d : displays) {
        if (!d.primary) {
            return &d;
        }
    }
    return &displays.front();
}

HMONITOR MonitorFromVirtualPoint(int x, int y)
{
    POINT pt{ x, y };
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
}

bool IsSameMonitor(HMONITOR a, HMONITOR b)
{
    return a != nullptr && b != nullptr && a == b;
}

} // namespace rm
