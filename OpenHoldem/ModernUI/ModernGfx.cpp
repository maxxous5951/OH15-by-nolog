//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Drawing primitives for the modern UI (GDI+).
//
//******************************************************************************

#include "ModernGfx.h"
#include "ModernSvg.h"
#include "ModernSuits.h"
#include <map>

namespace modernui {

using namespace Gdiplus;

namespace {

ULONG_PTR g_gdiplus_token = 0;
int g_gdiplus_refs = 0;

std::map<int, Font*>& FontCache() {
  static std::map<int, Font*> cache;
  return cache;
}

const FontFamily* UiFontFamily() {
  static FontFamily* family = nullptr;
  if (family == nullptr) {
    family = new FontFamily(L"Segoe UI");
    if (!family->IsAvailable()) {
      delete family;
      family = FontFamily::GenericSansSerif()->Clone();
    }
  }
  return family;
}

// Parsed icons, converted once.
typedef std::vector<PathSeg> Segs;
std::vector<std::vector<Segs> >& IconCache() {
  static std::vector<std::vector<Segs> > cache((size_t)Icon::Count_);
  return cache;
}

void BuildPath(const std::vector<PathSeg>& segs, GraphicsPath* gp) {
  PointF cur(0, 0), start(0, 0);
  for (size_t i = 0; i < segs.size(); ++i) {
    const PathSeg& s = segs[i];
    switch (s.type) {
      case PathSeg::Move:
        gp->StartFigure();
        cur = start = PointF(s.x[0], s.y[0]);
        break;
      case PathSeg::Line:
        gp->AddLine(cur, PointF(s.x[0], s.y[0]));
        cur = PointF(s.x[0], s.y[0]);
        break;
      case PathSeg::Cubic:
        gp->AddBezier(cur, PointF(s.x[0], s.y[0]), PointF(s.x[1], s.y[1]), PointF(s.x[2], s.y[2]));
        cur = PointF(s.x[2], s.y[2]);
        break;
      case PathSeg::Close:
        gp->CloseFigure();
        cur = start;
        break;
    }
  }
}

}  // namespace

void GdiplusAcquire() {
  if (g_gdiplus_refs++ == 0) {
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplus_token, &input, nullptr);
  }
}

void GdiplusRelease() {
  if (g_gdiplus_refs > 0 && --g_gdiplus_refs == 0) {
    ClearFontCache();
    GdiplusShutdown(g_gdiplus_token);
    g_gdiplus_token = 0;
  }
}

float DpiScale(HWND hwnd) {
  typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
  static GetDpiForWindowFn get_dpi = (GetDpiForWindowFn)(void*)GetProcAddress(
      GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  UINT dpi = 0;
  if (get_dpi != nullptr && hwnd != nullptr) dpi = get_dpi(hwnd);
  if (dpi == 0) {
    HDC dc = GetDC(nullptr);
    dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
  }
  if (dpi == 0) dpi = 96;
  return dpi / 96.0f;
}

const Font* GetFont(float px, bool bold) {
  int key = (int)(px * 10.0f + 0.5f) * 2 + (bold ? 1 : 0);
  std::map<int, Font*>& cache = FontCache();
  std::map<int, Font*>::iterator it = cache.find(key);
  if (it != cache.end()) return it->second;
  Font* font = new Font(UiFontFamily(), px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
  cache[key] = font;
  return font;
}

void ClearFontCache() {
  std::map<int, Font*>& cache = FontCache();
  for (std::map<int, Font*>::iterator it = cache.begin(); it != cache.end(); ++it) delete it->second;
  cache.clear();
}

void SetupQuality(Graphics& g) {
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  g.SetCompositingQuality(CompositingQualityHighQuality);
  g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
}

void DrawLabel(Graphics& g, const std::wstring& text, const Font* font, const RectF& rect,
               const Color& color, HAlign align, bool ellipsis) {
  if (text.empty() || font == nullptr) return;
  StringFormat fmt(StringFormat::GenericTypographic());
  // LineLimit must be off: otherwise text taller than its rectangle is not drawn at all.
  fmt.SetFormatFlags((fmt.GetFormatFlags() | StringFormatFlagsNoWrap) & ~StringFormatFlagsLineLimit);
  fmt.SetLineAlignment(StringAlignmentCenter);
  fmt.SetAlignment(align == HAlign::Left ? StringAlignmentNear
                   : align == HAlign::Center ? StringAlignmentCenter : StringAlignmentFar);
  fmt.SetTrimming(StringTrimmingNone);
  // Ellipsis is done by hand: the built-in trimming is not reliable with the flags above.
  std::wstring shown = text;
  if (ellipsis && MeasureLabel(g, shown, font) > rect.Width) {
    size_t lo = 0, hi = text.size();            // longest prefix that fits together with the dots
    while (lo < hi) {
      size_t mid = (lo + hi + 1) / 2;
      if (MeasureLabel(g, text.substr(0, mid) + L"\x2026", font) <= rect.Width) lo = mid; else hi = mid - 1;
    }
    shown = text.substr(0, lo) + L"\x2026";
  }
  SolidBrush brush(color);
  g.DrawString(shown.c_str(), (INT)shown.size(), font, rect, &fmt, &brush);
}

float MeasureLabel(Graphics& g, const std::wstring& text, const Font* font) {
  if (text.empty() || font == nullptr) return 0.0f;
  StringFormat fmt(StringFormat::GenericTypographic());
  fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
  RectF bounds;
  g.MeasureString(text.c_str(), (INT)text.size(), font, PointF(0, 0), &fmt, &bounds);
  return bounds.Width;
}

void AddRoundRect(GraphicsPath* path, const RectF& r, float radius) {
  float rad = radius;
  if (rad > r.Width / 2) rad = r.Width / 2;
  if (rad > r.Height / 2) rad = r.Height / 2;
  if (rad <= 0.5f) { path->AddRectangle(r); return; }
  float d = rad * 2;
  path->StartFigure();
  path->AddArc(r.X, r.Y, d, d, 180, 90);
  path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  path->CloseFigure();
}

void FillRoundRect(Graphics& g, const RectF& r, float radius, const Brush& b) {
  GraphicsPath path;
  AddRoundRect(&path, r, radius);
  g.FillPath(&b, &path);
}

void FillRoundRect(Graphics& g, const RectF& r, float radius, const Color& c) {
  SolidBrush brush(c);
  FillRoundRect(g, r, radius, brush);
}

void StrokeRoundRect(Graphics& g, const RectF& r, float radius, const Color& c, float width, DashStyle dash) {
  RectF inner(r.X + width / 2, r.Y + width / 2, r.Width - width, r.Height - width);
  GraphicsPath path;
  AddRoundRect(&path, inner, radius - width / 2);
  Pen pen(c, width);
  pen.SetDashStyle(dash);
  g.DrawPath(&pen, &path);
}

void FillCircle(Graphics& g, float cx, float cy, float radius, const Color& c) {
  SolidBrush brush(c);
  g.FillEllipse(&brush, cx - radius, cy - radius, radius * 2, radius * 2);
}

void DrawIcon(Graphics& g, Icon icon, const RectF& rect, const Color& color, float stroke_width) {
  size_t index = (size_t)icon;
  if (icon == Icon::None || index >= (size_t)Icon::Count_) return;
  std::vector<Segs>& parsed = IconCache()[index];
  if (parsed.empty()) {
    const IconDef& def = kIconDefs[index];
    for (int i = 0; i < def.count; ++i) {
      Segs segs;
      ParseSvgPath(def.paths[i], &segs);
      parsed.push_back(segs);
    }
  }
  GraphicsState state = g.Save();
  g.TranslateTransform(rect.X, rect.Y);
  g.ScaleTransform(rect.Width / 24.0f, rect.Height / 24.0f);
  Pen pen(color, stroke_width);
  pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
  pen.SetLineJoin(LineJoinRound);
  for (size_t i = 0; i < parsed.size(); ++i) {
    GraphicsPath gp;
    BuildPath(parsed[i], &gp);
    g.DrawPath(&pen, &gp);
  }
  g.Restore(state);
}

void DrawSuit(Graphics& g, char suit, const RectF& rect, const Color& color) {
  const char* const* paths = nullptr;
  int count = 0;
  switch (suit) {
    case 'h': case 'H': paths = kSuitHeart;   count = 1; break;
    case 'd': case 'D': paths = kSuitDiamond; count = 1; break;
    case 's': case 'S': paths = kSuitSpade;   count = 1; break;
    case 'c': case 'C': paths = kSuitClub;    count = 4; break;
    default: return;
  }
  static std::map<const char* const*, std::vector<Segs> > cache;
  std::vector<Segs>& parsed = cache[paths];
  if (parsed.empty()) {
    for (int i = 0; i < count; ++i) {
      Segs segs;
      ParseSvgPath(paths[i], &segs);
      parsed.push_back(segs);
    }
  }
  GraphicsState state = g.Save();
  g.TranslateTransform(rect.X, rect.Y);
  g.ScaleTransform(rect.Width / 24.0f, rect.Height / 24.0f);
  SolidBrush brush(color);
  GraphicsPath gp(FillModeWinding);   // union of overlapping shapes (club)
  for (size_t i = 0; i < parsed.size(); ++i) BuildPath(parsed[i], &gp);
  g.FillPath(&brush, &gp);
  g.Restore(state);
}

BufferedPaint::BufferedPaint(HWND hwnd)
    : hwnd_(hwnd), hdc_(nullptr), mem_dc_(nullptr), bitmap_(nullptr), old_bitmap_(nullptr),
      width_(0), height_(0) {
  ZeroMemory(&ps_, sizeof(ps_));
  hdc_ = BeginPaint(hwnd_, &ps_);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  width_ = rc.right - rc.left;
  height_ = rc.bottom - rc.top;
  if (width_ < 1) width_ = 1;
  if (height_ < 1) height_ = 1;
  mem_dc_ = CreateCompatibleDC(hdc_);
  bitmap_ = CreateCompatibleBitmap(hdc_, width_, height_);
  old_bitmap_ = SelectObject(mem_dc_, bitmap_);
  graphics_.reset(new Graphics(mem_dc_));
  SetupQuality(*graphics_);
}

BufferedPaint::~BufferedPaint() {
  graphics_.reset();
  BitBlt(hdc_, 0, 0, width_, height_, mem_dc_, 0, 0, SRCCOPY);
  SelectObject(mem_dc_, old_bitmap_);
  DeleteObject(bitmap_);
  DeleteDC(mem_dc_);
  EndPaint(hwnd_, &ps_);
}

}  // namespace modernui
