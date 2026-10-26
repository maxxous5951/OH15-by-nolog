//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: OpenHoldem Stats Explorer, Windows version (Win32 + DirectX 11):
//   window around the ImGui interface, built by StatsExplorer.sln.
//   usage: StatsExplorer.exe [path-to-oh_stats.db]
//
//******************************************************************************

#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <stdio.h>
#include <string>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "explorer_model.h"
#include "explorer_ui.h"

static ID3D11Device            *g_device = nullptr;
static ID3D11DeviceContext     *g_context = nullptr;
static IDXGISwapChain          *g_swap_chain = nullptr;
static bool                     g_occluded = false;
static UINT                     g_resize_w = 0, g_resize_h = 0;
static ID3D11RenderTargetView  *g_target = nullptr;

static void CreateRenderTarget() {
  ID3D11Texture2D *back_buffer = nullptr;
  g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  g_device->CreateRenderTargetView(back_buffer, nullptr, &g_target);
  back_buffer->Release();
}

static void CleanupRenderTarget() {
  if (g_target) { g_target->Release(); g_target = nullptr; }
}

static bool CreateDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  D3D_FEATURE_LEVEL level;
  const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
    D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
  if (hr == DXGI_ERROR_UNSUPPORTED) {   // no GPU driver: software renderer
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
      D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
  }
  if (hr != S_OK) return false;
  CreateRenderTarget();
  return true;
}

static void CleanupDevice() {
  CleanupRenderTarget();
  if (g_swap_chain) { g_swap_chain->Release(); g_swap_chain = nullptr; }
  if (g_context)    { g_context->Release();    g_context = nullptr; }
  if (g_device)     { g_device->Release();     g_device = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
  switch (msg) {
    case WM_SIZE:
      if (wparam == SIZE_MINIMIZED) return 0;
      g_resize_w = (UINT)LOWORD(lparam);
      g_resize_h = (UINT)HIWORD(lparam);
      return 0;
    case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0;   // no ALT menu
      break;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
  }
  return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

static std::string ToUtf8(const wchar_t *text) {
  int n = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return std::string();
  std::string out((size_t)n - 1, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], n, nullptr, nullptr);
  return out;
}

static bool FileExists(const std::wstring &path) {
  DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  ImGui_ImplWin32_EnableDpiAwareness();
  WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, ::GetModuleHandleW(nullptr), nullptr, nullptr,
                     nullptr, nullptr, L"OpenHoldemStatsExplorer", nullptr };
  ::RegisterClassExW(&wc);
  HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"OpenHoldem Stats Explorer", WS_OVERLAPPEDWINDOW,
                              100, 100, 1500, 900, nullptr, nullptr, wc.hInstance, nullptr);
  if (!CreateDevice(hwnd)) {
    CleanupDevice();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    ::MessageBoxW(nullptr, L"DirectX 11 could not be initialised.", L"Stats Explorer", MB_ICONERROR);
    return 1;
  }
  ::ShowWindow(hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  // Folder of the program: font (with accents), default database
  wchar_t module_path[MAX_PATH];
  ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  std::wstring folder = module_path;
  folder = folder.substr(0, folder.find_last_of(L"\\/") + 1);
  const std::wstring font = folder + L"fonts\\DroidSans.ttf";
  if (FileExists(font)) io.Fonts->AddFontFromFileTTF(ToUtf8(font.c_str()).c_str(), 16.0f);

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);

  explorer::Explorer ex;
  explorer::UiState ui;
  std::wstring path;
  int argc = 0;
  wchar_t **argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv && argc > 1) path = argv[1];
  if (argv) ::LocalFree(argv);
  if (path.empty() || !FileExists(path)) path = folder + L"Hand_Histories\\oh_stats.db";
  if (!FileExists(path)) path = folder + L"oh_stats.db";
  if (!FileExists(path)) path = L"oh_stats.db";
  const std::string path_utf8 = ToUtf8(path.c_str());
  if (FileExists(path)) {
    std::string error;
    if (!ex.Open(path_utf8, &error)) ex.error = error;
  } else {
    ex.error = "No database found. File > Open database... (oh_stats.db is created by OpenHoldem), "
               "or File > Create demo database...";
  }
  snprintf(ui.path_buffer, sizeof(ui.path_buffer), "%s", path_utf8.c_str());

  bool done = false;
  while (!done && !ui.quit_requested) {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;
    if (g_occluded && g_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
      ::Sleep(10);
      continue;
    }
    g_occluded = false;
    if (g_resize_w != 0 && g_resize_h != 0) {
      CleanupRenderTarget();
      g_swap_chain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
      g_resize_w = g_resize_h = 0;
      CreateRenderTarget();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    explorer::DrawExplorer(ex, ui);
    ImGui::Render();
    const float clear[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = g_swap_chain->Present(1, 0);
    g_occluded = (hr == DXGI_STATUS_OCCLUDED);
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupDevice();
  ::DestroyWindow(hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
  return 0;
}
