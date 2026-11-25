/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The part of GDI the engine draws with, for the POSIX port. DSurface keeps its pixels in a
// DIB section and draws a few lines of text and the occasional stretched copy with GDI;
// platform/posix/gdicompat.cpp provides those over plain memory.
//
// Only 16-bit bitfield DIB sections exist. Text is rendered with FreeType from the Arimo
// font in the ui directory, which has Arial's metrics, whatever face the caller names.

#pragma once

#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020
#define COLORONCOLOR 3
#define HALFTONE 4
#define TRANSPARENT 1
#define OPAQUE 2
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define FW_DONTCARE 0
#define FW_NORMAL 400
#define FW_BOLD 700
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define OUT_RASTER_PRECIS 6
#define OUT_TT_PRECIS 4
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define PROOF_QUALITY 2
#define ANTIALIASED_QUALITY 4
#define NONANTIALIASED_QUALITY 3
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define FF_DONTCARE 0
#define FF_ROMAN 16
#define FF_SWISS 32
#define FF_MODERN 48
#define GM_COMPATIBLE 1
#define GM_ADVANCED 2
#define MWT_IDENTITY 1
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define LF_FACESIZE 32

typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;

typedef struct tagBITMAPINFO {
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

typedef struct tagBITMAP {
	LONG bmType;
	LONG bmWidth;
	LONG bmHeight;
	LONG bmWidthBytes;
	WORD bmPlanes;
	WORD bmBitsPixel;
	void * bmBits;
} BITMAP;

typedef struct tagDIBSECTION {
	BITMAP dsBm;
	BITMAPINFOHEADER dsBmih;
	DWORD dsBitfields[3];
	HANDLE dshSection;
	DWORD dsOffset;
} DIBSECTION;

typedef struct tagLOGFONTA {
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, LOGFONT, *LPLOGFONT;

typedef struct tagTEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	BYTE tmFirstChar;
	BYTE tmLastChar;
	BYTE tmDefaultChar;
	BYTE tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
} TEXTMETRICA, TEXTMETRIC, *LPTEXTMETRIC;

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateDIBSection(HDC dc, BITMAPINFO const * info, UINT usage, void ** bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
int GetObjectA(HGDIOBJ object, int size, void * buffer);
#define GetObject GetObjectA
BOOL DeleteObject(HGDIOBJ object);
BOOL GdiFlush(void);
int SetStretchBltMode(HDC dc, int mode);
BOOL StretchBlt(HDC dest, int dx, int dy, int dw, int dh, HDC source, int sx, int sy, int sw, int sh, DWORD rop);
BOOL BitBlt(HDC dest, int dx, int dy, int w, int h, HDC source, int sx, int sy, DWORD rop);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
int GetDeviceCaps(HDC dc, int index);
static inline int MulDiv(int number, int numerator, int denominator)
{
	if (denominator == 0) return -1;
	return (int)(((long long)number * numerator + (denominator / 2)) / denominator);
}

HFONT CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic,
	DWORD underline, DWORD strikeout, DWORD charset, DWORD outprecision, DWORD clipprecision,
	DWORD quality, DWORD pitchandfamily, char const * face);
#define CreateFont CreateFontA
HFONT CreateFontIndirectA(LOGFONTA const * font);
#define CreateFontIndirect CreateFontIndirectA
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
UINT SetTextAlign(HDC dc, UINT align);
BOOL TextOutA(HDC dc, int x, int y, char const * text, int length);
#define TextOut TextOutA
BOOL GetTextExtentPoint32A(HDC dc, char const * text, int length, SIZE * size);
#define GetTextExtentPoint32 GetTextExtentPoint32A
BOOL GetTextMetricsA(HDC dc, TEXTMETRICA * metrics);
#define GetTextMetrics GetTextMetricsA

static inline int SaveDC(HDC) { return 1; }
static inline BOOL RestoreDC(HDC, int) { return TRUE; }
static inline int SetGraphicsMode(HDC, int) { return GM_COMPATIBLE; }
static inline BOOL ModifyWorldTransform(HDC, void const *, DWORD) { return TRUE; }
static inline BOOL SetViewportOrgEx(HDC, int, int, POINT *) { return TRUE; }
static inline BOOL SetWindowOrgEx(HDC, int, int, POINT *) { return TRUE; }
static inline BOOL DPtoLP(HDC, POINT *, int) { return TRUE; }
