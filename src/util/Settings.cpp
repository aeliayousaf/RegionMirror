#include "pch.h"
#include "Settings.h"

#include <windows.h>
#include <ShlObj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "Shell32.lib")

namespace rm {
namespace {

std::wstring Trim(const std::wstring& s)
{
    size_t b = 0;
    while (b < s.size() && iswspace(s[b])) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && iswspace(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

bool ParseLine(const std::wstring& line, std::wstring& key, std::wstring& value)
{
    const auto pos = line.find(L'=');
    if (pos == std::wstring::npos) {
        return false;
    }
    key = Trim(line.substr(0, pos));
    value = Trim(line.substr(pos + 1));
    return !key.empty();
}

} // namespace

std::wstring SettingsStore::SettingsDirectory()
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path))) {
        result = path;
        CoTaskMemFree(path);
        result += L"\\RegionMirror";
    }
    return result;
}

std::wstring SettingsStore::SettingsFilePath()
{
    return SettingsDirectory() + L"\\settings.ini";
}

AppSettings SettingsStore::Load()
{
    AppSettings settings;
    const auto path = SettingsFilePath();
    std::wifstream in(path);
    if (!in) {
        return settings;
    }

    std::wstring line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        std::wstring key;
        std::wstring value;
        if (!ParseLine(line, key, value)) {
            continue;
        }
        if (key == L"OutputMonitorId") {
            settings.outputMonitorId = value;
        } else if (key == L"OutputMode") {
            if (value == L"Fullscreen" || value == L"1") {
                settings.outputMode = OutputMode::Fullscreen;
            } else {
                settings.outputMode = OutputMode::Windowed;
            }
        } else if (key == L"ShowDebug") {
            settings.showDebug = (value == L"1" || _wcsicmp(value.c_str(), L"true") == 0);
        } else if (key == L"OpacityPercent") {
            try {
                int v = std::stoi(value);
                if (v < 10) {
                    v = 10;
                }
                if (v > 100) {
                    v = 100;
                }
                settings.opacityPercent = v;
            } catch (...) {
            }
        } else if (key == L"AlwaysOnTop") {
            settings.alwaysOnTop = (value == L"1" || _wcsicmp(value.c_str(), L"true") == 0);
        } else if (key == L"SharpenPercent") {
            try {
                int v = std::stoi(value);
                if (v < 0) {
                    v = 0;
                }
                if (v > 100) {
                    v = 100;
                }
                settings.sharpenPercent = v;
            } catch (...) {
            }
        }
    }
    return settings;
}

void SettingsStore::Save(const AppSettings& settings)
{
    const auto dir = SettingsDirectory();
    if (dir.empty()) {
        return;
    }
    CreateDirectoryW(dir.c_str(), nullptr);

    std::wofstream out(SettingsFilePath(), std::ios::trunc);
    if (!out) {
        return;
    }
    out << L"OutputMonitorId=" << settings.outputMonitorId << L"\n";
    out << L"OutputMode=" << (settings.outputMode == OutputMode::Fullscreen ? L"Fullscreen" : L"Windowed") << L"\n";
    out << L"ShowDebug=" << (settings.showDebug ? L"1" : L"0") << L"\n";
    out << L"OpacityPercent=" << settings.opacityPercent << L"\n";
    out << L"AlwaysOnTop=" << (settings.alwaysOnTop ? L"1" : L"0") << L"\n";
    out << L"SharpenPercent=" << settings.sharpenPercent << L"\n";
}

} // namespace rm
