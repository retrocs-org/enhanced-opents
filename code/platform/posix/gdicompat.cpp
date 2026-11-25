/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// GDI over plain memory for the POSIX port; see gdicompat.h.

#include "wincompat.h"
#include "posixpaths.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace {

enum GdiKind { GDI_DC = 0x44434443, GDI_BITMAP = 0x424D4250, GDI_FONT = 0x464F4E54 };

struct GdiObject
{
	GdiKind Kind;
};

struct GdiBitmap : GdiObject
{
	int Width;
	int Height;
	int Pitch;
	unsigned short * Bits;
};

struct GdiFont : GdiObject
{
	int PixelHeight;
	bool Bold;
};

struct GdiDC : GdiObject
{
	GdiBitmap * Bitmap;
	GdiFont * Font;
	GdiBitmap Stock;
	GdiFont StockFont;
	COLORREF TextColor;
	COLORREF BackColor;
	int BackMode;
	UINT Align;
};

template<class T>
T * As(HANDLE handle, GdiKind kind)
{
	GdiObject * object = (GdiObject *)handle;
	return (object != nullptr && object->Kind == kind) ? (T *)object : nullptr;
}

std::mutex FontLock;
FT_Library Library = nullptr;
FT_Face Face = nullptr;
bool FaceTried = false;

FT_Face Font_Face(int pixelheight)
{
	if (!FaceTried) {
		FaceTried = true;
		if (FT_Init_FreeType(&Library) == 0) {
			std::string const path = Posix_UI_Directory() + "Arimo.ttf";
			if (FT_New_Face(Library, path.c_str(), 0, &Face) != 0) {
				Face = nullptr;
			}
		}
	}
	if (Face != nullptr) {
		FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)std::max(pixelheight, 1));
	}
	return Face;
}

// GDI takes a negative height as the character height and a positive one as the cell height.
int Pixel_Height(int height)
{
	if (height == 0) return 16;
	return height < 0 ? -height : (height * 7 + 4) / 8;
}

unsigned short To_565(COLORREF color)
{
	unsigned r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
	return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void Blend_565(unsigned short & pixel, unsigned short color, unsigned alpha)
{
	if (alpha == 0) return;
	if (alpha >= 255) { pixel = color; return; }
	unsigned dr = pixel >> 11, dg = (pixel >> 5) & 63, db = pixel & 31;
	unsigned sr = color >> 11, sg = (color >> 5) & 63, sb = color & 31;
	dr = (sr * alpha + dr * (255 - alpha)) / 255;
	dg = (sg * alpha + dg * (255 - alpha)) / 255;
	db = (sb * alpha + db * (255 - alpha)) / 255;
	pixel = (unsigned short)((dr << 11) | (dg << 5) | db);
}

// Decodes one UTF-8 sequence, taking a stray byte as Windows-1252 would have.
unsigned Next_Code(char const * & text, char const * end)
{
	unsigned char c = (unsigned char)*text++;
	if (c < 0x80) return c;
	int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
	if (extra == 0 || text + extra > end) return c;
	unsigned code = c & (0x3F >> extra);
	for (int i = 0; i < extra; i++) {
		unsigned char next = (unsigned char)text[i];
		if ((next & 0xC0) != 0x80) return c;
		code = (code << 6) | (next & 0x3F);
	}
	text += extra;
	return code;
}

void Measure(GdiFont const * font, char const * text, int length, int & width, int & ascent, int & descent)
{
	width = 0;
	ascent = 0;
	descent = 0;
	int const height = font != nullptr ? font->PixelHeight : 16;
	FT_Face face = Font_Face(height);
	if (face == nullptr) {
		width = length * height / 2;
		ascent = height * 4 / 5;
		descent = height - ascent;
		return;
	}
	ascent = (int)(face->size->metrics.ascender >> 6);
	descent = (int)(-(face->size->metrics.descender >> 6));
	char const * end = text + length;
	while (text < end) {
		unsigned code = Next_Code(text, end);
		if (FT_Load_Char(face, code, FT_LOAD_DEFAULT) == 0) {
			width += (int)(face->glyph->advance.x >> 6);
		}
	}
}

}


HDC CreateCompatibleDC(HDC)
{
	GdiDC * dc = new GdiDC();
	dc->Kind = GDI_DC;
	dc->Stock.Kind = GDI_BITMAP;
	dc->Stock.Width = 1;
	dc->Stock.Height = 1;
	dc->Stock.Pitch = 2;
	dc->Stock.Bits = nullptr;
	dc->Bitmap = &dc->Stock;
	dc->StockFont.Kind = GDI_FONT;
	dc->StockFont.PixelHeight = 16;
	dc->StockFont.Bold = false;
	dc->Font = &dc->StockFont;
	dc->TextColor = RGB(0, 0, 0);
	dc->BackColor = RGB(255, 255, 255);
	dc->BackMode = OPAQUE;
	dc->Align = TA_LEFT | TA_TOP;
	return dc;
}


BOOL DeleteDC(HDC handle)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr) return FALSE;
	dc->Kind = (GdiKind)0;
	delete dc;
	return TRUE;
}


HBITMAP CreateDIBSection(HDC, BITMAPINFO const * info, UINT, void ** bits, HANDLE, DWORD)
{
	if (bits != nullptr) *bits = nullptr;
	if (info == nullptr || info->bmiHeader.biBitCount != 16) return nullptr;

	int const width = info->bmiHeader.biWidth;
	int const height = std::abs((int)info->bmiHeader.biHeight);
	if (width <= 0 || height <= 0) return nullptr;

	GdiBitmap * bitmap = new GdiBitmap();
	bitmap->Kind = GDI_BITMAP;
	bitmap->Width = width;
	bitmap->Height = height;
	bitmap->Pitch = ((width * 2) + 3) & ~3;
	bitmap->Bits = (unsigned short *)calloc((size_t)bitmap->Pitch * height, 1);
	if (bitmap->Bits == nullptr) {
		delete bitmap;
		return nullptr;
	}
	if (bits != nullptr) *bits = bitmap->Bits;
	return bitmap;
}


HGDIOBJ SelectObject(HDC handle, HGDIOBJ object)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr || object == nullptr) return nullptr;

	if (GdiBitmap * bitmap = As<GdiBitmap>(object, GDI_BITMAP)) {
		GdiBitmap * old = dc->Bitmap;
		dc->Bitmap = bitmap;
		return old;
	}
	if (GdiFont * font = As<GdiFont>(object, GDI_FONT)) {
		GdiFont * old = dc->Font;
		dc->Font = font;
		return old;
	}
	return nullptr;
}


int GetObjectA(HGDIOBJ object, int size, void * buffer)
{
	GdiBitmap * bitmap = As<GdiBitmap>(object, GDI_BITMAP);
	if (bitmap == nullptr || buffer == nullptr) return 0;

	if (size >= (int)sizeof(DIBSECTION)) {
		DIBSECTION * section = (DIBSECTION *)buffer;
		memset(section, 0, sizeof(*section));
		section->dsBm.bmWidth = bitmap->Width;
		section->dsBm.bmHeight = bitmap->Height;
		section->dsBm.bmWidthBytes = bitmap->Pitch;
		section->dsBm.bmPlanes = 1;
		section->dsBm.bmBitsPixel = 16;
		section->dsBm.bmBits = bitmap->Bits;
		section->dsBmih.biSize = sizeof(BITMAPINFOHEADER);
		section->dsBmih.biWidth = bitmap->Width;
		section->dsBmih.biHeight = -bitmap->Height;
		section->dsBmih.biPlanes = 1;
		section->dsBmih.biBitCount = 16;
		section->dsBmih.biCompression = BI_BITFIELDS;
		section->dsBitfields[0] = 0xF800;
		section->dsBitfields[1] = 0x07E0;
		section->dsBitfields[2] = 0x001F;
		return sizeof(DIBSECTION);
	}
	if (size >= (int)sizeof(BITMAP)) {
		BITMAP * bm = (BITMAP *)buffer;
		memset(bm, 0, sizeof(*bm));
		bm->bmWidth = bitmap->Width;
		bm->bmHeight = bitmap->Height;
		bm->bmWidthBytes = bitmap->Pitch;
		bm->bmPlanes = 1;
		bm->bmBitsPixel = 16;
		bm->bmBits = bitmap->Bits;
		return sizeof(BITMAP);
	}
	return 0;
}


BOOL DeleteObject(HGDIOBJ object)
{
	if (GdiBitmap * bitmap = As<GdiBitmap>(object, GDI_BITMAP)) {
		free(bitmap->Bits);
		bitmap->Kind = (GdiKind)0;
		delete bitmap;
		return TRUE;
	}
	if (GdiFont * font = As<GdiFont>(object, GDI_FONT)) {
		font->Kind = (GdiKind)0;
		delete font;
		return TRUE;
	}
	return FALSE;
}


BOOL GdiFlush(void)
{
	return TRUE;
}


int SetStretchBltMode(HDC, int)
{
	return COLORONCOLOR;
}


BOOL StretchBlt(HDC desthandle, int dx, int dy, int dw, int dh, HDC sourcehandle, int sx, int sy, int sw, int sh, DWORD)
{
	GdiDC * dest = As<GdiDC>(desthandle, GDI_DC);
	GdiDC * source = As<GdiDC>(sourcehandle, GDI_DC);
	if (dest == nullptr || source == nullptr || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return FALSE;

	GdiBitmap const * from = source->Bitmap;
	GdiBitmap * to = dest->Bitmap;
	if (from->Bits == nullptr || to->Bits == nullptr) return FALSE;

	for (int y = 0; y < dh; y++) {
		int const ty = dy + y;
		if (ty < 0 || ty >= to->Height) continue;
		int const fy = sy + (int)(((long long)y * sh) / dh);
		if (fy < 0 || fy >= from->Height) continue;

		unsigned short const * srcrow = (unsigned short const *)((char const *)from->Bits + (size_t)fy * from->Pitch);
		unsigned short * dstrow = (unsigned short *)((char *)to->Bits + (size_t)ty * to->Pitch);

		for (int x = 0; x < dw; x++) {
			int const tx = dx + x;
			if (tx < 0 || tx >= to->Width) continue;
			int const fx = sx + (int)(((long long)x * sw) / dw);
			if (fx < 0 || fx >= from->Width) continue;
			dstrow[tx] = srcrow[fx];
		}
	}
	return TRUE;
}


BOOL BitBlt(HDC dest, int dx, int dy, int w, int h, HDC source, int sx, int sy, DWORD rop)
{
	return StretchBlt(dest, dx, dy, w, h, source, sx, sy, w, h, rop);
}


// A screen context stands for the display: it measures text but has no pixels.
HDC GetDC(HWND)
{
	return CreateCompatibleDC(nullptr);
}


int ReleaseDC(HWND, HDC dc)
{
	return DeleteDC(dc) ? 1 : 0;
}


int GetDeviceCaps(HDC, int index)
{
	return (index == LOGPIXELSX || index == LOGPIXELSY) ? 96 : 0;
}


HFONT CreateFontA(int height, int, int, int, int weight, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, char const *)
{
	GdiFont * font = new GdiFont();
	font->Kind = GDI_FONT;
	font->PixelHeight = Pixel_Height(height);
	font->Bold = weight >= FW_BOLD;
	return font;
}


HFONT CreateFontIndirectA(LOGFONTA const * info)
{
	if (info == nullptr) return nullptr;
	return CreateFontA(info->lfHeight, info->lfWidth, 0, 0, info->lfWeight, 0, 0, 0, 0, 0, 0, 0, 0, info->lfFaceName);
}


COLORREF SetTextColor(HDC handle, COLORREF color)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr) return 0;
	COLORREF old = dc->TextColor;
	dc->TextColor = color;
	return old;
}


COLORREF SetBkColor(HDC handle, COLORREF color)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr) return 0;
	COLORREF old = dc->BackColor;
	dc->BackColor = color;
	return old;
}


int SetBkMode(HDC handle, int mode)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr) return 0;
	int old = dc->BackMode;
	dc->BackMode = mode;
	return old;
}


UINT SetTextAlign(HDC handle, UINT align)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr) return 0;
	UINT old = dc->Align;
	dc->Align = align;
	return old;
}


BOOL GetTextExtentPoint32A(HDC handle, char const * text, int length, SIZE * size)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (size == nullptr || text == nullptr) return FALSE;
	std::lock_guard<std::mutex> guard(FontLock);
	int width, ascent, descent;
	Measure(dc != nullptr ? dc->Font : nullptr, text, length, width, ascent, descent);
	size->cx = width;
	size->cy = ascent + descent;
	return TRUE;
}


BOOL GetTextMetricsA(HDC handle, TEXTMETRICA * metrics)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (metrics == nullptr) return FALSE;
	std::lock_guard<std::mutex> guard(FontLock);
	memset(metrics, 0, sizeof(*metrics));
	int width, ascent, descent;
	Measure(dc != nullptr ? dc->Font : nullptr, "x", 1, width, ascent, descent);
	metrics->tmAscent = ascent;
	metrics->tmDescent = descent;
	metrics->tmHeight = ascent + descent;
	metrics->tmAveCharWidth = width;
	metrics->tmMaxCharWidth = width * 2;
	metrics->tmWeight = (dc != nullptr && dc->Font->Bold) ? FW_BOLD : FW_NORMAL;
	metrics->tmPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
	return TRUE;
}


BOOL TextOutA(HDC handle, int x, int y, char const * text, int length)
{
	GdiDC * dc = As<GdiDC>(handle, GDI_DC);
	if (dc == nullptr || text == nullptr || length <= 0) return FALSE;
	GdiBitmap * bitmap = dc->Bitmap;
	if (bitmap->Bits == nullptr) return FALSE;

	std::lock_guard<std::mutex> guard(FontLock);

	int width, ascent, descent;
	Measure(dc->Font, text, length, width, ascent, descent);

	if ((dc->Align & TA_CENTER) == TA_CENTER) {
		x -= width / 2;
	} else if (dc->Align & TA_RIGHT) {
		x -= width;
	}
	int baseline = y + ascent;
	if ((dc->Align & TA_BASELINE) == TA_BASELINE) {
		baseline = y;
	} else if (dc->Align & TA_BOTTOM) {
		baseline = y - descent;
	}

	unsigned short const color = To_565(dc->TextColor);

	if (dc->BackMode == OPAQUE) {
		unsigned short const back = To_565(dc->BackColor);
		for (int row = baseline - ascent; row < baseline + descent; row++) {
			if (row < 0 || row >= bitmap->Height) continue;
			unsigned short * line = (unsigned short *)((char *)bitmap->Bits + (size_t)row * bitmap->Pitch);
			for (int column = std::max(x, 0); column < std::min(x + width, bitmap->Width); column++) {
				line[column] = back;
			}
		}
	}

	FT_Face face = Font_Face(dc->Font->PixelHeight);
	if (face == nullptr) return TRUE;

	char const * end = text + length;
	int penx = x;
	while (text < end) {
		unsigned code = Next_Code(text, end);
		if (FT_Load_Char(face, code, FT_LOAD_RENDER) != 0) continue;

		// Bold is drawn twice, one pixel apart, rather than through FreeType's emboldening.
		FT_GlyphSlot glyph = face->glyph;
		FT_Bitmap const & image = glyph->bitmap;
		int const left = penx + glyph->bitmap_left;
		int const top = baseline - glyph->bitmap_top;

		for (int passes = dc->Font->Bold ? 2 : 1, pass = 0; pass < passes; pass++) {
			for (unsigned row = 0; row < image.rows; row++) {
				int const ty = top + (int)row;
				if (ty < 0 || ty >= bitmap->Height) continue;
				unsigned short * line = (unsigned short *)((char *)bitmap->Bits + (size_t)ty * bitmap->Pitch);
				unsigned char const * coverage = image.buffer + row * image.pitch;
				for (unsigned column = 0; column < image.width; column++) {
					int const tx = left + pass + (int)column;
					if (tx < 0 || tx >= bitmap->Width) continue;
					Blend_565(line[tx], color, coverage[column]);
				}
			}
		}
		penx += (int)(glyph->advance.x >> 6);
	}
	return TRUE;
}
