#pragma once

namespace OpenScopeBuild
{
#if defined(NDEBUG)
inline constexpr bool kDebugBuild = false;
#else
inline constexpr bool kDebugBuild = true;
#endif
}
