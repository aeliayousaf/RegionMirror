#include "pch.h"
#include "App.h"
#include "util/CrashLog.h"

namespace {

LONG WINAPI RegionMirrorUnhandledException(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord) {
        void* address = info->ExceptionRecord->ExceptionAddress;
        wchar_t buf[512];
        HMODULE module = nullptr;
        wchar_t modulePath[MAX_PATH] = L"(unknown)";
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address),
                &module) && module) {
            GetModuleFileNameW(module, modulePath, MAX_PATH);
        }
        swprintf_s(buf, L"Unhandled exception code=0x%08X address=%p module=%s",
                   info->ExceptionRecord->ExceptionCode,
                   address,
                   modulePath);
        rm::CrashLog(buf);
    } else {
        rm::CrashLog(L"Unhandled exception (no details)");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

int APIENTRY wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR,
    _In_ int)
{
    SetUnhandledExceptionFilter(RegionMirrorUnhandledException);

    // Per-Monitor DPI Awareness V2 is also declared in the application manifest.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    rm::CrashLog(L"Process start");
    rm::App app(instance);
    const int rc = app.Run();
    rm::CrashLog(L"Process exit");
    return rc;
}
