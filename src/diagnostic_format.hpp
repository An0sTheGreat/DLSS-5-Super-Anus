#pragma once
#include <cstdio>
#include <cstddef>

// Standard bounded formatting supports the 64-bit values that wsprintfA lost.
// Uses Windows' UCRT import, not a second statically initialized CRT in the graft.
template <std::size_t N, typename... Args>
void diagnostic_format(char (&buffer)[N], const char *format, Args... args)
{
    static_assert(N >= 16);
    const int count = std::snprintf(buffer, N, format, args...);
    if (count < 0)
        std::snprintf(buffer, N, "%s", "[format error]");
    else if (static_cast<std::size_t>(count) >= N)
    {
        constexpr char suffix[] = " [truncated]";
        for (std::size_t i = 0; i < sizeof(suffix); ++i)
            buffer[N - sizeof(suffix) + i] = suffix[i];
    }
}
