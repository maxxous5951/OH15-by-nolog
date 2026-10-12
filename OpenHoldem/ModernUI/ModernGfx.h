//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Drawing primitives for the modern UI (GDI+): rounded rectangles,
//   text, vector icons, card suits, flicker-free painting, DPI helpers.
//
//******************************************************************************

#ifndef INC_MODERNUI_GFX_H
#define INC_MODERNUI_GFX_H

#include "ModernCommon.h"
#include "ModernIconData.h"
#include <memory>

namespace modernui {

// GDI+ must be started once per process. Calls are reference counted.
void GdiplusAcquire();
void GdiplusRelease();

// Scale factor of the window (1.0 == 96 dpi).
float DpiScale(HWND hwnd);

// Cached font (Segoe UI). "px" is the em height in device pixels.
const Gdiplus::Font* GetFont(float px, bool bold = false);
void ClearFontCache();

enum class HAlign { Left, Center, Right };

// Anti-aliased, ClearType text, one line, with optional ellipsis.
void SetupQuality(Gdiplus::Graphics& g);
void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font* font,
               const Gdiplus::RectF& rect, const Gdiplus::Color& color,
               HAlign align = HAlign::Left, bool ellipsis = true);
float MeasureLabel(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font* font);

// Rounded rectangles
void AddRoundRect(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& r, float radius);
void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Color& c);
void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Brush& b);
// The stroke is drawn INSIDE the rectangle (so that it never gets clipped).
void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                     const Gdiplus::Color& c, float width = 1.0f,
                     Gdiplus::DashStyle dash = Gdiplus::DashStyleSolid);
void FillCircle(Gdiplus::Graphics& g, float cx, float cy, float radius, const Gdiplus::Color& c);

// Vector icons (Lucide, 24x24 grid, stroke based).
void DrawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color, float stroke_width = 2.0f);

// Card suit as filled shape. suit: 'c', 'd', 'h' or 's' (case insensitive).
void DrawSuit(Gdiplus::Graphics& g, char suit, const Gdiplus::RectF& rect, const Gdiplus::Color& color);

// Double-buffered painting for a WM_PAINT handler.
class BufferedPaint {
 public:
  explicit BufferedPaint(HWND hwnd);
  ~BufferedPaint();
  Gdiplus::Graphics& G() { return *graphics_; }
  int Width() const  { return width_; }
  int Height() const { return height_; }
 private:
  HWND hwnd_;
  PAINTSTRUCT ps_;
  HDC hdc_;
  HDC mem_dc_;
  HBITMAP bitmap_;
  HGDIOBJ old_bitmap_;
  int width_, height_;
  std::unique_ptr<Gdiplus::Graphics> graphics_;
};

}  // namespace modernui

#endif  // INC_MODERNUI_GFX_H
