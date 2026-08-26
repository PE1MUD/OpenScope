#pragma once

namespace OpenScopeBuild
{
#if defined(NDEBUG)
inline constexpr bool kDebugBuild = false;
#else
inline constexpr bool kDebugBuild = true;
#endif

// TEMPORARY 0.8.3 focus/timing investigation switch.
// Keep the finite non-blocking TraceLogger active in Release as well as Debug
// so scheduler/timer behaviour can be measured with production timing.
// Set back to kDebugBuild after the focus-timing investigation.
inline constexpr bool kTraceLoggingEnabled = true;
}
