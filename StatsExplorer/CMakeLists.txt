# OpenHoldem Stats Explorer: browser for the stats database (oh_stats.db)
# Windows / Linux / macOS. Needs CMake >= 3.14, a C++14 compiler and OpenGL.
# Dear ImGui and GLFW are downloaded by CMake (FetchContent); to use local copies:
#   cmake -S . -B build -DIMGUI_DIR=/path/to/imgui -DGLFW_DIR=/path/to/glfw
cmake_minimum_required(VERSION 3.14)
project(StatsExplorer CXX C)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(IMGUI_DIR)
  set(imgui_SOURCE_DIR ${IMGUI_DIR})
else()
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9)
  FetchContent_MakeAvailable(imgui)
endif()
if(GLFW_DIR)
  add_subdirectory(${GLFW_DIR} glfw)
else()
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)   # Linux: X11 only (no wayland-scanner needed)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
  FetchContent_MakeAvailable(glfw)
endif()
find_package(OpenGL REQUIRED)

set(OH ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(sqlite3 STATIC ${OH}/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${OH}/sqlite)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# The part without GUI: database + model + interface (shared by the program and the test)
add_library(explorer_core STATIC
  ${OH}/OpenHoldem/StatsDb.cpp src/explorer_model.cpp src/explorer_ui.cpp src/demo_data.cpp)
target_include_directories(explorer_core PUBLIC src ${OH}/OpenHoldem)
target_link_libraries(explorer_core PUBLIC sqlite3 imgui)

add_executable(StatsExplorer src/main.cpp)
target_link_libraries(StatsExplorer PRIVATE explorer_core)
add_custom_command(TARGET StatsExplorer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:StatsExplorer>/fonts)

# Headless test (software rendering, no window): ctest or build target "headless_render"
add_executable(headless_render tests/headless_render.cpp)
target_link_libraries(headless_render PRIVATE explorer_core)
enable_testing()
add_test(NAME headless_render
  COMMAND headless_render ${imgui_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
