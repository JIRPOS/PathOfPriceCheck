cmake_minimum_required(VERSION 3.20)

# MAJOR.MINOR comes from the VERSION file; BUILD is injected by CI (-DAPP_BUILD=<run number>).
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" VERSION_BASE)
if(NOT DEFINED APP_BUILD)
  set(APP_BUILD 0)
endif()
set(APP_VERSION "${VERSION_BASE}.${APP_BUILD}")

project(PathOfPriceCheck VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.12
  GIT_SHALLOW TRUE)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(SDL3 imgui nlohmann_json)

# Dear ImGui ships no CMake target we want; compile core + backends ourselves.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_definitions(imgui PUBLIC SDL_MAIN_HANDLED)

set(APP_SOURCES
  src/main.cpp
  src/app.cpp
  src/config.cpp
  src/fonts.cpp
  src/overlay.cpp
  src/platform/input.cpp
  src/platform/keymap_sdl.cpp
  src/screens/settings_screen.cpp
  src/screens/pricecheck_screen.cpp)
if(WIN32)
  list(APPEND APP_SOURCES
    src/platform/hotkeys_win.cpp src/platform/foreground_win.cpp src/platform/input_sim_win.cpp
    src/platform/overlay_native_win.cpp src/platform/clipboard_win.cpp)
else()
  list(APPEND APP_SOURCES
    src/platform/hotkeys_x11.cpp src/platform/foreground_x11.cpp src/platform/input_sim_x11.cpp
    src/platform/overlay_native_x11.cpp src/platform/clipboard_x11.cpp)
endif()

add_executable(PathOfPriceCheck ${APP_SOURCES})
target_include_directories(PathOfPriceCheck PRIVATE src)
target_compile_definitions(PathOfPriceCheck PRIVATE APP_VERSION="${APP_VERSION}" SDL_MAIN_HANDLED)
target_link_libraries(PathOfPriceCheck PRIVATE SDL3::SDL3 imgui nlohmann_json::nlohmann_json)

find_package(OpenGL REQUIRED)
target_link_libraries(PathOfPriceCheck PRIVATE OpenGL::GL)
find_package(Threads REQUIRED)
target_link_libraries(PathOfPriceCheck PRIVATE Threads::Threads)

if(WIN32)
  target_link_libraries(PathOfPriceCheck PRIVATE user32)
else()
  find_package(X11 REQUIRED)
  if(NOT TARGET X11::Xtst)
    message(FATAL_ERROR "libXtst (libxtst-dev) is required for synthetic input")
  endif()
  if(NOT TARGET X11::Xext)
    message(FATAL_ERROR "libXext (libxext-dev) is required for the overlay input shape")
  endif()
  target_link_libraries(PathOfPriceCheck PRIVATE X11::X11 X11::Xtst X11::Xext)
endif()

enable_testing()
add_executable(smoke_test tests/smoke_test.cpp)
add_test(NAME smoke COMMAND smoke_test)
