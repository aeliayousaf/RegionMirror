#pragma once

#include <windows.h>
#include <fstream>
#include <string>

namespace rm {

inline void CrashLog(const std::wstring& line)
{
    try {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
            return;
        }
        std::wstring file = path;
        const auto slash = file.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            file = file.substr(0, slash + 1);
        }
        file += L"regionmirror.log";

        std::wofstream out(file, std::ios::app);
        if (!out) {
            return;
        }
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t stamp[64];
        swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        out << stamp << line << L"\n";
    } catch (...) {
    }
}

} // namespace rm
