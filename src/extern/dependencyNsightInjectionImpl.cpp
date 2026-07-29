#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Detects whether NVIDIA Nsight Graphics is actively intercepting this process.
// Nsight loads its interception runtime into the target only while launching or
// attaching, so the module's presence is a precise "intercepting now" signal,
// unlike enumerating the installed implicit layer (which is also present on a
// direct launch). Exposed to the engine via the `dependency` module wrapper.
extern "C" bool nrPlatformNsightInjected() noexcept
{
#if defined(_WIN32)
    return ::GetModuleHandleW(L"Nvda.Graphics.Interception.dll") != nullptr;
#else
    return false;
#endif
}
