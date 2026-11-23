#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace rm {

struct DisplayInfo {
    HMONITOR monitor = nullptr;
    std::wstring deviceName;   // \\.\DISPLAYN
    std::wstring friendlyName; // where available
    RECT bounds{};             // virtual desktop pixels (rcMonitor)
    RECT workArea{};           // rcWork
    bool primary = false;
    UINT dpi = 96;

    std::wstring label() const;
    std::wstring preferenceId() const;
};

std::vector<DisplayInfo> EnumerateDisplays();

const DisplayInfo* FindDisplayByPreferenceId(const std::vector<DisplayInfo>& displays, const std::wstring& id);
const DisplayInfo* FindDisplayByMonitor(const std::vector<DisplayInfo>& displays, HMONITOR monitor);
const DisplayInfo* DefaultOutputDisplay(const std::vector<DisplayInfo>& displays);

HMONITOR MonitorFromVirtualPoint(int x, int y);
bool IsSameMonitor(HMONITOR a, HMONITOR b);

} // namespace rm
