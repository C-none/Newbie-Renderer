module;
#include "staticUtilsConstants.h"
export module nr.utils:staticUtils;
import std;

export namespace nr
{
// Export the compile-time constants from staticUtilsConstants.h
using ::nr::isDebugMode;
using ::nr::maxThreads;
using ::nr::LogLevel;
using ::nr::globalLogLevel;
using ::nr::logLevelNames;

// Application constants
inline constexpr uint32_t maxFrameInFlight = 3;
[[nodiscard]] consteval unsigned VK_MAKE_API_VERSION(unsigned variant, unsigned major, unsigned minor, unsigned patch)
{
    // substitue the macros defined in vk_platform.h
    return ((((variant)) << 29U) | (((major)) << 22U) | (((minor)) << 12U) | ((patch)));
}

} // namespace nr
