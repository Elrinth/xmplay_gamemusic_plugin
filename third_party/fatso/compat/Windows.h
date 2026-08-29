/* Portable stand-in so NotSo Fatso cores build on Linux hosts. */
#ifndef FATSO_COMPAT_WINDOWS_H
#define FATSO_COMPAT_WINDOWS_H

#ifdef _WIN32
#include_next <windows.h>
#endif
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#if !defined(_WINDEF_) && !defined(_WINDEF_H)
typedef uint8_t BYTE;
typedef uint8_t UINT8;
typedef uint16_t WORD;
typedef uint16_t UINT16;
typedef uint32_t DWORD;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;
typedef unsigned int UINT;
typedef int BOOL;
typedef const char *LPCSTR;
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef ZeroMemory
#define ZeroMemory(p, n) memset((p), 0, (n))
#endif
#ifndef Sleep
#define Sleep(ms) ((void)(ms))
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __forceinline
#define __forceinline inline
#endif

#ifndef __forceinline
#if defined(_MSC_VER)
#define __forceinline __forceinline
#else
#define __forceinline inline
#endif
#endif

#ifndef lstrlen
#define lstrlen strlen
#endif
#ifndef __int64
#define __int64 long long
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif
