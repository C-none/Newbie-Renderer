#pragma once

// Nsight Graphics SDK 0.9.0 includes <shlobj_core.h>, which is available in
// the MSVC Windows Kit but not in the MinGW-w64 headers used by the LLVM Debug
// preset. The SDK code path used here does not require shlobj_core-specific
// declarations, and MinGW-w64 exposes the needed shell declarations through
// <shlobj.h>.
#include <shlobj.h>
