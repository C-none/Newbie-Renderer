#pragma once
#include <cstdint>
#include <array>
#include <string_view>

namespace nr
{

// Build configuration
#if defined(_DEBUG) || !defined(NDEBUG)
inline constexpr bool isDebugMode = true;
#else
inline constexpr bool isDebugMode = false;
#endif
inline constexpr uint32_t maxThreads = 32;

// Log level enumeration (auto-generated from CMake)
enum class LogLevel : uint32_t
{
    info = 0,
    warning = 1,
    error = 2,
    number = 3
};

// Current global log level (set at compile time)
inline constexpr LogLevel globalLogLevel = LogLevel::info;

// Display names for log levels
inline constexpr std::array<std::string_view, static_cast<size_t>(LogLevel::number)> logLevelNames = {"INFO", "WARNING", "ERROR"};

} // namespace nr
