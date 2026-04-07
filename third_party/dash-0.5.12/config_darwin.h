#pragma once

/* macOS-specific overrides for dash 0.5.12 config.h.
 *
 * Include the autoconf-generated config.h first, then un-define the three
 * features that are absent on Darwin / Apple Clang.  Each has an #ifndef
 * fallback in dash's source so simply undefining them is safe.
 *
 * Verified on macOS 26 / Apple clang 21, Apple Silicon (arm64).
 */
#include "config.h"

/* mempcpy is a GNU extension not present in macOS libc. */
#undef HAVE_MEMPCPY

/* GCC __alias__ attribute is not supported on Darwin (Apple clang). */
#undef HAVE_ALIAS_ATTRIBUTE

/* macOS 10.15+ removed the *64 transitional aliases.  On Darwin all these
 * types and functions are semantically identical to their non-suffixed
 * counterparts (already 64-bit by default).
 *
 * On x86_64 the SDK still provides stat64/lstat64/fstat64, so only
 * redefine those on arm64.  The remaining *64 symbols (open64, dirent64,
 * readdir64, glob64, globfree64) never existed on any macOS and must
 * always be redirected. */
#undef HAVE_ST_MTIM
#ifdef __arm64__
#define stat64     stat
#define lstat64    lstat
#define fstat64    fstat
#endif
#define open64     open
#define dirent64   dirent
#define readdir64  readdir
#define glob64     glob
#define globfree64 globfree
