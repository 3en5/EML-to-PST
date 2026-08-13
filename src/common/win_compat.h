// WLM2PST - single include point for <windows.h> in Windows-only code.
//
// Portable core code must NOT include this header. Windows-only translation
// units (suffix `_win.cpp`, the launcher exe, and src/worker/mapi/) include it
// to get a consistently configured windows.h.
#pragma once

#ifndef _WIN32
#error "win_compat.h included in a non-Windows translation unit"
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef STRICT
#define STRICT
#endif

#include <windows.h>
