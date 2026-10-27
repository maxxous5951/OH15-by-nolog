//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: OpenHoldem Stats Explorer: window (GLFW + OpenGL3) around the ImGui interface.
//   usage: StatsExplorer [path-to-oh_stats.db]
//
//******************************************************************************

#include <stdio.h>
#include <string.h>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "explorer_model.h"
#include "explorer_ui.h"

static void GlfwError(int code, const char *description) {
  fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

static bool FileExists(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  glfwSetErrorCallback(GlfwError);
  if (!glfwInit()) return 1;
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow *window = glfwCreateWindow(1500, 900, "OpenHoldem Stats Explorer", NULL, NULL);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = "stats_explorer.ini";
  ImGui::StyleColorsDark();
  // Font with accents (next to the program), else the built-in font
  const char *fonts[] = { "fonts/DroidSans.ttf", "../fonts/DroidSans.ttf", "DroidSans.ttf" };
  bool font_loaded = false;
  for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]) && !font_loaded; ++i) {
    if (FileExists(fonts[i])) font_loaded = io.Fonts->AddFontFromFileTTF(fonts[i], 16.0f) != NULL;
  }
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  explorer::Explorer ex;
  explorer::UiState ui;
  // Default database: argument, else next to the program, else the current folder
  std::string exe_dir = argv[0];
  { size_t cut = exe_dir.find_last_of("/\\"); exe_dir = cut == std::string::npos ? std::string() : exe_dir.substr(0, cut + 1); }
  std::string path = argc > 1 ? argv[1] : exe_dir + "Hand_Histories/oh_stats.db";
  if (!FileExists(path)) path = exe_dir + "oh_stats.db";
  if (!FileExists(path)) path = "Hand_Histories/oh_stats.db";
  if (!FileExists(path)) path = "oh_stats.db";
  if (FileExists(path)) {
    std::string error;
    if (!ex.Open(path, &error)) ex.error = error;
  } else {
    ex.error = "No database found. File > Open database... (oh_stats.db is created by OpenHoldem), "
               "or File > Create demo database...";
  }
  snprintf(ui.path_buffer, sizeof(ui.path_buffer), "%s", path.c_str());

  while (!glfwWindowShouldClose(window) && !ui.quit_requested) {
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.2); continue; }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    explorer::DrawExplorer(ex, ui);
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
