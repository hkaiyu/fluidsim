#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

// Types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float f32;
typedef double f64;

#define internal static
#define global static

#define KB(x) ((u64)(x)) << 10
#define MB(x) ((u64)(x)) << 20
#define GB(x) ((u64)(x)) << 30

// TODO: probably need decent logging at some point?
// We could implement thread-safe error querying directly without changing much
// code. We just change LOG_ERROR statements to some thread-local error
// accumulator.
void LogMessage(const char *level, const char *fmt, ...);
void LogFlush();

// General macros
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(min, x, max) MIN(MAX((min), (x)), (max))
#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])

// ANSI colors
#define COLOR_RESET "\x1b[0m"
#define COLOR_INFO "\x1b[38;5;42m"
#define COLOR_WARNING "\x1b[38;5;178m"
#define COLOR_SEVERE "\x1b[38;5;9m"

#define COLOR(string, ansi_color) ansi_color string COLOR_RESET

// Logging macros
#define INFO(fmt, ...)                                                         \
  LogMessage(COLOR("[INFO]", COLOR_INFO), fmt, ##__VA_ARGS__)
#define WARN(fmt, ...)                                                         \
  LogMessage(COLOR("[WARNING]", COLOR_WARNING), fmt, ##__VA_ARGS__); LogFlush()
#define FATAL(fmt, ...)                                                        \
  LogMessage(COLOR("[FATAL]", COLOR_SEVERE), fmt, ##__VA_ARGS__); LogFlush();  \
  abort()
#define REQUIRE(fmt, ...)                                                      \
  LogMessage(COLOR("[REQUIREMENT]", COLOR_SEVERE), fmt, ##__VA_ARGS__);        \
  exit(EXIT_SUCCESS)
