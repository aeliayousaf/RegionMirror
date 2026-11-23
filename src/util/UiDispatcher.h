#pragma once

#include <DispatcherQueue.h>
#include <winrt/Windows.System.h>

namespace rm {

// Win32 STA threads need a DispatcherQueue for WinRT capture events/async to work reliably.
inline winrt::Windows::System::DispatcherQueueController CreateUiDispatcherQueueController()
{
    DispatcherQueueOptions options{
        sizeof(DispatcherQueueOptions),
        DQTYPE_THREAD_CURRENT,
        DQTAT_COM_STA
    };

    ABI::Windows::System::IDispatcherQueueController* raw = nullptr;
    winrt::check_hresult(CreateDispatcherQueueController(options, &raw));
    return { raw, winrt::take_ownership_from_abi };
}

} // namespace rm
