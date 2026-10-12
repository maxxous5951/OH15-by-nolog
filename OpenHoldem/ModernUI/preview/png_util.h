// Preview harness helper: renders into a 32-bit GDI+ bitmap and saves it as PNG.
// Not part of the OpenHoldem build; only used to check the look under Wine/Windows.
#pragma once
#include "../ModernGfx.h"
#include "../ModernTheme.h"
#include <functional>

inline bool SavePng(Gdiplus::Bitmap& bmp, const wchar_t* path) {
  UINT n = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&n, &size);
  if (!size) return false;
  Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)malloc(size);
  Gdiplus::GetImageEncoders(n, size, info);
  CLSID clsid = {0}; bool found = false;
  for (UINT i = 0; i < n; ++i) if (wcscmp(info[i].MimeType, L"image/png") == 0) { clsid = info[i].Clsid; found = true; }
  free(info);
  return found && bmp.Save(path, &clsid, nullptr) == Gdiplus::Ok;
}

inline bool RenderToPng(int w, int h, const wchar_t* path, const std::function<void(Gdiplus::Graphics&)>& draw) {
  Gdiplus::Bitmap bmp(w, h, PixelFormat32bppRGB);
  Gdiplus::Graphics g(&bmp);
  modernui::SetupQuality(g);
  draw(g);
  return SavePng(bmp, path);
}
