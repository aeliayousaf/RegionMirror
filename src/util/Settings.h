#pragma once

#include <string>

namespace rm {

enum class OutputMode {
    Windowed = 0,
    Fullscreen = 1,
};

struct AppSettings {
    std::wstring outputMonitorId;
    OutputMode outputMode = OutputMode::Windowed;
    bool showDebug = false;
    // Mirror window opacity in percent (10-100). 100 = fully opaque.
    int opacityPercent = 100;
};

class SettingsStore {
public:
    static std::wstring SettingsDirectory();
    static std::wstring SettingsFilePath();

    static AppSettings Load();
    static void Save(const AppSettings& settings);
};

} // namespace rm
