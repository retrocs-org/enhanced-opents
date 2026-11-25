/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The subset of the Win32 API that the engine uses, for the POSIX port. CMake forces this
// header into every OpenTS translation unit, and the stub headers beside it (windows.h,
// mmsystem.h, winsock.h, ...) resolve to it, so engine sources compile unchanged.
//
// Types keep their Windows widths: DWORD and LONG are 32 bits even though long is 64 bits
// on macOS and Linux.

#pragma once

#if !defined(_WIN32)

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef __cplusplus
#include <cstdint>
#include <cmath>
#include <cstdarg>

// libstdc++ leaves the float-suffixed functions out of std; MSVC and libc++ have them.
// Repeating a using-declaration is harmless where they are already there.
namespace std {
	using ::sinf; using ::cosf; using ::tanf; using ::asinf; using ::acosf; using ::atanf;
	using ::atan2f; using ::sqrtf; using ::powf; using ::expf; using ::logf; using ::log10f;
	using ::floorf; using ::ceilf; using ::fabsf; using ::fmodf; using ::roundf; using ::truncf;
	using ::hypotf; using ::lroundf; using ::exp2f; using ::log2f; using ::sinhf; using ::coshf;
	using ::tanhf; using ::copysignf; using ::fminf; using ::fmaxf; using ::cbrtf;
}
#endif

#define OPENTS_WINCOMPAT 1

#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef _cdecl
#define _cdecl
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#ifndef __int64
#define __int64 long long
#endif
#ifndef __int32
#define __int32 int
#endif
#ifndef __int16
#define __int16 short
#endif
#ifndef __int8
#define __int8 char
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef VOID
#define VOID void
#endif

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int INT;
typedef unsigned int UINT;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef wchar_t WCHAR;
typedef float FLOAT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t DWORD64;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef size_t SIZE_T;
typedef BYTE BOOLEAN;
typedef char * LPSTR;
typedef char const * LPCSTR;
typedef char * PSTR;
typedef char TCHAR;
typedef char * LPTSTR;
typedef char const * LPCTSTR;
typedef wchar_t * LPWSTR;
typedef wchar_t const * LPCWSTR;
typedef void * LPVOID;
typedef void const * LPCVOID;
typedef void * PVOID;
typedef DWORD * LPDWORD;
typedef DWORD * PDWORD;
typedef BYTE * LPBYTE;
typedef BYTE * PBYTE;
typedef BOOL * LPBOOL;
typedef LONG * LPLONG;
typedef WORD * LPWORD;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef LONG HRESULT;
typedef WORD ATOM;
typedef DWORD COLORREF;

typedef void * HANDLE;
typedef HANDLE HWND;
typedef HANDLE HINSTANCE;
typedef HANDLE HMODULE;
typedef HANDLE HDC;
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HBRUSH;
typedef HANDLE HMENU;
typedef HANDLE HFONT;
typedef HANDLE HBITMAP;
typedef HANDLE HGDIOBJ;
typedef HANDLE HPALETTE;
typedef HANDLE HGLOBAL;
typedef HANDLE HRSRC;
typedef HANDLE HKEY;
typedef HANDLE HMONITOR;
typedef HANDLE HACCEL;
typedef void * FARPROC;
typedef HANDLE * LPHANDLE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAKEWORD(a, b) ((WORD)(((BYTE)((a) & 0xff)) | ((WORD)((BYTE)((b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((a) & 0xffff)) | ((DWORD)((WORD)((b) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define CP_UTF8 65001
#define CP_ACP 0
#define ERROR_SUCCESS 0L

typedef struct tagPOINT { LONG x; LONG y; } POINT, *LPPOINT, *PPOINT;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT, *LPRECT, *PRECT;
typedef RECT const * LPCRECT;
typedef struct tagSIZE { LONG cx; LONG cy; } SIZE, *LPSIZE;

typedef union _LARGE_INTEGER {
	struct { DWORD LowPart; LONG HighPart; };
	struct { DWORD LowPart; LONG HighPart; } u;
	LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
	struct { DWORD LowPart; DWORD HighPart; };
	struct { DWORD LowPart; DWORD HighPart; } u;
	ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
	WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

// Only the CRT spellings the engine uses are mapped: macOS names FILE members _read,
// _write, _seek and _close, so wider aliasing would reach into its stdio.h.
#define stricmp strcasecmp
#define _stricmp strcasecmp
#define strcmpi strcasecmp
#define strnicmp strncasecmp
#define _strnicmp strncasecmp
#define memicmp strncasecmp
#define _unlink unlink
#define _fileno fileno
#define _O_BINARY 0
#define O_BINARY 0
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR O_RDWR
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR
#define _Printf_format_string_
#define TEXT(text) text

#ifndef _MAX_PATH
#define _MAX_DRIVE 3
#define _MAX_DIR   256
#define _MAX_FNAME 256
#define _MAX_EXT   256
#define _MAX_PATH  512
#define MAX_PATH   _MAX_PATH
#endif


/*
**	Timing, sleeping and debugger output (platform/posix/wincompat.cpp).
*/
DWORD timeGetTime(void);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
void Sleep(DWORD milliseconds);
BOOL QueryPerformanceCounter(LARGE_INTEGER * count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency);
void OutputDebugStringA(char const * text);
#define OutputDebugString OutputDebugStringA
DWORD GetLastError(void);
void SetLastError(DWORD error);
DWORD GetCurrentProcessId(void);
DWORD GetCurrentThreadId(void);
UINT GetACP(void);
void GetLocalTime(SYSTEMTIME * time);
void GetSystemTime(SYSTEMTIME * time);
DWORD GetModuleFileNameA(HMODULE module, char * buffer, DWORD size);
#define GetModuleFileName GetModuleFileNameA
DWORD GetCurrentDirectoryA(DWORD size, char * buffer);
#define GetCurrentDirectory GetCurrentDirectoryA
BOOL SetCurrentDirectoryA(char const * path);
#define SetCurrentDirectory SetCurrentDirectoryA
BOOL CreateDirectoryA(char const * path, void * security);
#define CreateDirectory CreateDirectoryA
BOOL DeleteFileA(char const * path);
#define DeleteFile DeleteFileA
BOOL MoveFileA(char const * from, char const * to);
#define MoveFile MoveFileA
BOOL CopyFileA(char const * from, char const * to, BOOL fail_if_exists);
#define CopyFile CopyFileA
DWORD GetFileAttributesA(char const * path);
#define GetFileAttributes GetFileAttributesA
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_READONLY 0x01
int MessageBoxA(HWND window, char const * text, char const * caption, UINT type);
#define MessageBox MessageBoxA
#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_ABORTRETRYIGNORE 0x2
#define MB_YESNOCANCEL 0x3
#define MB_YESNO 0x4
#define MB_RETRYCANCEL 0x5
#define MB_ICONHAND 0x10
#define MB_ICONERROR 0x10
#define MB_ICONSTOP 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONEXCLAMATION 0x30
#define MB_ICONWARNING 0x30
#define MB_ICONASTERISK 0x40
#define MB_ICONINFORMATION 0x40
#define MB_SETFOREGROUND 0x10000
#define MB_TOPMOST 0x40000
#define MB_TASKMODAL 0x2000
#define MB_SYSTEMMODAL 0x1000
#define MB_APPLMODAL 0x0
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

/*
**	Files and directories (platform/posix/wincompat.cpp). A HANDLE from CreateFile wraps a
**	file descriptor; a HANDLE from FindFirstFile wraps a directory scan.
*/
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define FILE_SHARE_DELETE 0x4
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define FILE_ATTRIBUTE_HIDDEN 0x02
#define FILE_ATTRIBUTE_SYSTEM 0x04
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define MOVEFILE_REPLACE_EXISTING 0x1
#define MOVEFILE_COPY_ALLOWED 0x2
#define MOVEFILE_WRITE_THROUGH 0x8
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INSUFFICIENT_BUFFER 122L

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[MAX_PATH];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
typedef WIN32_FIND_DATAA * LPWIN32_FIND_DATA;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;

HANDLE CreateFileA(char const * name, DWORD access, DWORD share, void * security, DWORD disposition, DWORD flags, HANDLE templ);
#define CreateFile CreateFileA
BOOL ReadFile(HANDLE file, void * buffer, DWORD count, DWORD * read, void * overlapped);
BOOL WriteFile(HANDLE file, void const * buffer, DWORD count, DWORD * written, void * overlapped);
BOOL CloseHandle(HANDLE handle);
DWORD GetFileSize(HANDLE file, DWORD * high);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER * size);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG * high, DWORD method);
BOOL SetEndOfFile(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
BOOL MoveFileExA(char const * from, char const * to, DWORD flags);
#define MoveFileEx MoveFileExA
BOOL ReplaceFileA(char const * replaced, char const * replacement, char const * backup, DWORD flags, void *, void *);
#define ReplaceFile ReplaceFileA
BOOL RemoveDirectoryA(char const * path);
#define RemoveDirectory RemoveDirectoryA
BOOL GetFileAttributesExA(char const * path, GET_FILEEX_INFO_LEVELS level, void * info);
#define GetFileAttributesEx GetFileAttributesExA
BOOL GetDiskFreeSpaceExA(char const * path, ULARGE_INTEGER * available, ULARGE_INTEGER * total, ULARGE_INTEGER * free);
#define GetDiskFreeSpaceEx GetDiskFreeSpaceExA
DWORD GetTempPathA(DWORD size, char * buffer);
#define GetTempPath GetTempPathA
DWORD GetFullPathNameA(char const * name, DWORD size, char * buffer, char ** part);
#define GetFullPathName GetFullPathNameA
HANDLE FindFirstFileA(char const * pattern, WIN32_FIND_DATAA * data);
#define FindFirstFile FindFirstFileA
BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA * data);
#define FindNextFile FindNextFileA
BOOL FindClose(HANDLE find);
long filelength(int fd);

/*
**	Time.
*/
BOOL SystemTimeToFileTime(SYSTEMTIME const * system, FILETIME * file);
BOOL FileTimeToSystemTime(FILETIME const * file, SYSTEMTIME * system);
BOOL FileTimeToLocalFileTime(FILETIME const * file, FILETIME * local);
BOOL LocalFileTimeToFileTime(FILETIME const * local, FILETIME * file);
void GetSystemTimeAsFileTime(FILETIME * file);
LONG CompareFileTime(FILETIME const * a, FILETIME const * b);
BOOL FileTimeToDosDateTime(FILETIME const * file, WORD * date, WORD * time);
BOOL DosDateTimeToFileTime(WORD date, WORD time, FILETIME * file);
#define LOCALE_USER_DEFAULT 0x400
#define LANG_USER_DEFAULT 0x400
#define LOCALE_SYSTEM_DEFAULT 0x800
#define TIME_NOMINUTESORSECONDS 0x1
#define TIME_NOSECONDS 0x2
#define TIME_NOTIMEMARKER 0x4
#define TIME_FORCE24HOURFORMAT 0x8
#define DATE_SHORTDATE 0x1
#define DATE_LONGDATE 0x2
int GetDateFormatA(DWORD locale, DWORD flags, SYSTEMTIME const * time, char const * format, char * buffer, int size);
#define GetDateFormat GetDateFormatA
int GetTimeFormatA(DWORD locale, DWORD flags, SYSTEMTIME const * time, char const * format, char * buffer, int size);
#define GetTimeFormat GetTimeFormatA
static inline unsigned int timeBeginPeriod(unsigned int) { return 0; }
static inline unsigned int timeEndPeriod(unsigned int) { return 0; }

/*
**	Text conversion. wchar_t is 32 bits here, so a wide string holds code points.
*/
int WideCharToMultiByte(UINT codepage, DWORD flags, wchar_t const * wide, int widecount, char * multi, int multicount, char const * defchar, BOOL * useddef);
int MultiByteToWideChar(UINT codepage, DWORD flags, char const * multi, int multicount, wchar_t * wide, int widecount);
wchar_t * CharUpperW(wchar_t * text);
#define MB_ERR_INVALID_CHARS 0x8
#define WC_NO_BEST_FIT_CHARS 0x400

/*
**	Memory.
*/
void * _aligned_malloc(size_t size, size_t alignment);
void * _aligned_realloc(void * block, size_t size, size_t alignment);
void _aligned_free(void * block);
typedef struct _MEMORYSTATUS {
	DWORD dwLength;
	DWORD dwMemoryLoad;
	SIZE_T dwTotalPhys;
	SIZE_T dwAvailPhys;
	SIZE_T dwTotalPageFile;
	SIZE_T dwAvailPageFile;
	SIZE_T dwTotalVirtual;
	SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
void GlobalMemoryStatus(MEMORYSTATUS * status);
static inline HANDLE LocalFree(HANDLE block) { free(block); return NULL; }

/*
**	Floating point control. The POSIX port always runs with the default environment:
**	round to nearest, exceptions masked, denormals kept.
*/
#define _MCW_EM 0x0008001F
#define _MCW_RC 0x00000300
#define _MCW_PC 0x00030000
#define _MCW_DN 0x03000000
#define _CW_DEFAULT 0x0009001F
unsigned int _controlfp(unsigned int value, unsigned int mask);
int _controlfp_s(unsigned int * current, unsigned int value, unsigned int mask);

/*
**	Windowing. SDL owns the window; these exist so the shared code compiles.
*/
typedef HANDLE HKL;
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXDRAG 68
#define SM_CYDRAG 69
int GetSystemMetrics(int index);
// The live state of a key or mouse button as SDL last saw it, in GetKeyState's form: the
// high bit for held, the low bit for a lock key that is on.
SHORT GetKeyState(int virtualkey);
#define GetAsyncKeyState GetKeyState
static inline UINT GetDoubleClickTime(void) { return 500; }
static inline HKL GetKeyboardLayout(DWORD) { return NULL; }
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
static inline UINT MapVirtualKeyExW(UINT, UINT, HKL) { return 0; }
static inline UINT MapVirtualKeyA(UINT, UINT) { return 0; }
#define MapVirtualKey MapVirtualKeyA
static inline void InitCommonControls(void) {}
static inline LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
static inline LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
#define DefWindowProc DefWindowProcA
static inline HMODULE GetModuleHandleA(char const *) { return NULL; }
static inline HMODULE GetModuleHandleW(wchar_t const *) { return NULL; }
#define GetModuleHandle GetModuleHandleA
static inline UINT GetWindowsDirectoryA(char *, UINT) { return 0; }
#define GetWindowsDirectory GetWindowsDirectoryA
static inline BOOL AttachConsole(DWORD) { return FALSE; }
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
static inline BOOL AllocConsole(void) { return FALSE; }
static inline BOOL FreeConsole(void) { return TRUE; }
static inline int freopen_s(FILE ** stream, char const * name, char const * mode, FILE * old)
{
	FILE * opened = freopen(name, mode, old);
	if (stream != NULL) *stream = opened;
	return opened != NULL ? 0 : errno;
}
static inline int fopen_s(FILE ** stream, char const * name, char const * mode)
{
	*stream = fopen(name, mode);
	return *stream != NULL ? 0 : errno;
}
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))
#define WM_USER 0x0400
#define WM_APP 0x8000
#define WM_NULL 0x0000
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ACTIVATEAPP 0x001C
#define WM_CANCELMODE 0x001F
#define WM_CAPTURECHANGED 0x0215
#define WM_EXITSIZEMOVE 0x0232
#define WM_EXITMENULOOP 0x0212
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_RESTORE 9

/*
**	Virtual-key codes. The engine keeps Windows key numbers internally; sdl/sdlkeys.cpp
**	translates SDL scancodes to them.
*/
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_SLEEP 0x5F
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_VOLUME_MUTE 0xAD
#define VK_VOLUME_DOWN 0xAE
#define VK_VOLUME_UP 0xAF
#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define VK_MEDIA_STOP 0xB2
#define VK_MEDIA_PLAY_PAUSE 0xB3
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2

/*
**	Version resources do not exist off Windows.
*/
static inline DWORD GetFileVersionInfoSizeA(char const *, DWORD * handle) { if (handle) *handle = 0; return 0; }
#define GetFileVersionInfoSize GetFileVersionInfoSizeA
static inline BOOL GetFileVersionInfoA(char const *, DWORD, DWORD, void *) { return FALSE; }
#define GetFileVersionInfo GetFileVersionInfoA
static inline BOOL VerQueryValueA(void const *, char const *, void **, UINT * length) { if (length) *length = 0; return FALSE; }
#define VerQueryValue VerQueryValueA

// Describes a GetLastError code; dbgprint.h declares this for Windows.
char const * Last_Error_Text(uint32_t error);

#include "gdicompat.h"

/*
**	The language strings, compiled in from language.rc (platform/posix/langstrings.cpp).
*/
HMODULE LoadLibraryA(char const * name);
#define LoadLibrary LoadLibraryA
BOOL FreeLibrary(HMODULE module);
FARPROC GetProcAddress(HMODULE module, char const * name);
int LoadStringA(HINSTANCE instance, UINT id, char * buffer, int size);
#define LoadString LoadStringA

/*
**	Compiler intrinsics MSVC provides.
*/

// Clang declares these as builtins when -fms-extensions is on; GCC does not. _lrotl and
// _lrotr rotate a Windows long, which is 32 bits, so they map to the 32-bit forms.
#if defined(__has_builtin)
#if !__has_builtin(_rotl)
static inline unsigned int _rotl(unsigned int value, int shift) { shift &= 31; return shift == 0 ? value : (value << shift) | (value >> (32 - shift)); }
static inline unsigned int _rotr(unsigned int value, int shift) { shift &= 31; return shift == 0 ? value : (value >> shift) | (value << (32 - shift)); }
#endif
#if !__has_builtin(_byteswap_ulong)
static inline unsigned short _byteswap_ushort(unsigned short v) { return __builtin_bswap16(v); }
static inline unsigned int _byteswap_ulong(unsigned int v) { return __builtin_bswap32(v); }
static inline unsigned long long _byteswap_uint64(unsigned long long v) { return __builtin_bswap64(v); }
#endif
#endif
#define _lrotl(value, shift) _rotl((unsigned int)(value), (shift))
#define _lrotr(value, shift) _rotr((unsigned int)(value), (shift))
// x86 has the time stamp counter and CPUID, which clang provides under -fms-extensions. ARM
// reads its generic timer instead and reports no CPUID leaves.
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
// ia32intrin.h spells the rotates as macros where the builtins above are meant.
#undef _rotl
#undef _rotr
#undef _lrotl
#undef _lrotr
#define _lrotl(value, shift) _rotl((unsigned int)(value), (shift))
#define _lrotr(value, shift) _rotr((unsigned int)(value), (shift))
#elif defined(__aarch64__)
static inline unsigned long long __rdtsc(void)
{
	unsigned long long value;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
	return value;
}
static inline void __cpuid(int regs[4], int) { regs[0] = regs[1] = regs[2] = regs[3] = 0; }
#endif

#define __debugbreak() __builtin_trap()
#define _alloca __builtin_alloca

#endif // !_WIN32
