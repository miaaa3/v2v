#pragma once

#include <cstdio>

// Minimal logging macros — replaced with a real logger later if needed.
// Usage: V2V_LOG_INFO("loaded %d nodes", count);

// `__VA_OPT__` is standard C++20 — it inserts the comma only when variadic
// arguments are present, so this is warning-free (unlike the GNU `##` hack).
#define V2V_LOG_INFO(fmt, ...)  std::fprintf(stdout, "[INFO ] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define V2V_LOG_WARN(fmt, ...)  std::fprintf(stderr, "[WARN ] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define V2V_LOG_ERROR(fmt, ...) std::fprintf(stderr, "[ERROR] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
