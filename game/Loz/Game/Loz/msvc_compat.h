/*
   Portability shims for MSVC-specific functions/macros used by Loz.
   Force-included (before everything) on non-Windows builds via CMake.
*/
#pragma once

#ifndef _WIN32

#include <cstdio>
#include <cstdarg>
#include <cstddef>
#include <cerrno>
#include <strings.h>    // strcasecmp
#include <sys/stat.h>   // mkdir

typedef int errno_t;

#ifdef __3DS__
// Game assets made from the player's ROM live in memory, see
// zelda3ds/source/AssetStore.h. Every other path falls through to fopen.
FILE* AssetStore_Open(const char* path, const char* mode);
#define LOZ_ASSET_FOPEN AssetStore_Open
#else
#define LOZ_ASSET_FOPEN fopen
#endif

// MSVC: errno_t fopen_s(FILE**, const char* name, const char* mode)
static inline errno_t loz_fopen_s(FILE** f, const char* name, const char* mode)
{
    if (f == nullptr) return EINVAL;
    *f = LOZ_ASSET_FOPEN(name, mode);
    return (*f != nullptr) ? 0 : (errno != 0 ? errno : EIO);
}
#define fopen_s loz_fopen_s

// MSVC secure template form: sprintf_s(char (&buf)[N], fmt, ...) -> no size arg.
template <size_t N>
static inline int sprintf_s(char (&buf)[N], const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, N, fmt, args);
    va_end(args);
    return r;
}

// Element count of a fixed-size array.
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define _stricmp strcasecmp

// MSVC: int _mkdir(const char* path)  -> POSIX mkdir with default perms.
static inline int loz_mkdir(const char* path) { return mkdir(path, 0755); }
#define _mkdir loz_mkdir

// MSVC debug-report macros: route to stderr (no-op-ish, keeps shader logs etc).
#define _CRT_WARN   0
#define _CRT_ERROR  1
#define _CRT_ASSERT 2
#define _RPT0(t, fmt)                 fprintf(stderr, fmt)
#define _RPT1(t, fmt, a)              fprintf(stderr, fmt, a)
#define _RPT2(t, fmt, a, b)           fprintf(stderr, fmt, a, b)
#define _RPT3(t, fmt, a, b, c)        fprintf(stderr, fmt, a, b, c)
#define _RPT4(t, fmt, a, b, c, d)     fprintf(stderr, fmt, a, b, c, d)

#endif // !_WIN32
