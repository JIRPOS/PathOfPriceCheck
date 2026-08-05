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

FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.12
  GIT_SHALLOW TRUE)

# libcurl: system package where there is one, source build on Windows. Static with Schannel
# so the release stays a single .exe with no DLL beside it and no CA bundle to ship.
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
  if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  endif()
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  # CURL_ZLIB=ON is find_package(ZLIB REQUIRED) inside curl, and Windows has no system zlib, so
  # bring our own: gzip stays on for the multi-megabyte trade-data endpoints on every platform
  # rather than silently degrading to identity wherever the dependency happens to be missing.
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.2
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(zlib)
  # Only the shared build gets a ZLIB::ZLIB alias upstream, and curl links that name. Pointing
  # FindZLIB's two result variables at the fetched tree short-circuits its search; the target
  # already existing stops it importing a second one over ours.
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
  set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
  set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
  set(CURL_ZLIB ON CACHE STRING "" FORCE)
  # One JSON GET and a few file downloads; everything else is dead weight.
  foreach(proto DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP)
    set(CURL_DISABLE_${proto} ON CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_11_1
    GIT_SHALLOW TRUE)
  # curl aliases CURL::libcurl onto the static lib itself once BUILD_SHARED_LIBS is off; declaring
  # it here as well is a duplicate-target error, not a no-op.
  FetchContent_MakeAvailable(curl)
endif()

# CMake 4.4 warns twice about the pinned dependencies. Both are settled here rather than in the
# vendored trees, and unset afterwards so our own code keeps getting the diagnostics.
# SDL 3.4.12 escapes its ELF-dlopen-note probe for pre-CMP0219 macro expansion, so OLD is the
# behaviour it was written against; NEW hands check_c_source_compiles() a different C snippet.
set(CMAKE_POLICY_DEFAULT_CMP0219 OLD)
# doctest 2.4.12 still declares cmake_minimum_required(VERSION 3.5). Raising its floor is what this
# variable is for; do not reach for CMAKE_WARN_DEPRECATED, which CMP0218 made a no-op.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
FetchContent_MakeAvailable(SDL3 imgui nlohmann_json doctest)
unset(CMAKE_POLICY_VERSION_MINIMUM)
unset(CMAKE_POLICY_DEFAULT_CMP0219)

# Dear ImGui ships no CMake target we want; compile core + backends ourselves.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                        ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_definitions(imgui PUBLIC SDL_MAIN_HANDLED)

# Everything that needs neither a window nor a network lives here, so it can be unit-tested.
# The rule: ppc_core links no SDL3, no ImGui, no X11, no libcurl.
add_library(ppc_core STATIC
  src/paths.cpp
  src/config.cpp
  src/leagues.cpp
  src/util/sha256.cpp
  src/data/index.cpp
  src/data/stat_normalize.cpp
  src/data/game_data.cpp
  src/data/stat_matcher.cpp
  src/data/manifest.cpp
  src/data/install.cpp
  src/item/item.cpp
  src/item/parse.cpp
  src/item/resolve.cpp
  src/item/derive.cpp
  src/item/plan.cpp
  $<IF:$<BOOL:${WIN32}>,src/data/mapped_file_win.cpp,src/data/mapped_file_posix.cpp>
  src/platform/input.cpp)
target_include_directories(ppc_core PUBLIC src)
target_compile_definitions(ppc_core PUBLIC APP_VERSION="${APP_VERSION}")
target_link_libraries(ppc_core PUBLIC nlohmann_json::nlohmann_json)

set(APP_SOURCES
  src/main.cpp
  src/app.cpp
  src/fonts.cpp
  src/icon.cpp
  src/overlay.cpp
  src/league_service.cpp
  src/data/updater.cpp
  src/net/http.cpp
  src/platform/keymap_sdl.cpp
  src/screens/settings_screen.cpp
  src/screens/pricecheck_screen.cpp
  src/screens/item_view.cpp)
if(WIN32)
  list(APPEND APP_SOURCES
    src/platform/hotkeys_win.cpp src/platform/foreground_win.cpp src/platform/input_sim_win.cpp
    src/platform/overlay_native_win.cpp src/platform/clipboard_win.cpp)
  # Explorer/taskbar icon for the .exe; the tray and window icons come from src/icon.cpp.
  enable_language(RC)
  set(PPC_ICO_PATH "${CMAKE_SOURCE_DIR}/assets/popc_icon.ico")
  configure_file(assets/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
  list(APPEND APP_SOURCES "${CMAKE_BINARY_DIR}/app.rc")
else()
  list(APPEND APP_SOURCES
    src/platform/hotkeys_x11.cpp src/platform/foreground_x11.cpp src/platform/input_sim_x11.cpp
    src/platform/overlay_native_x11.cpp src/platform/clipboard_x11.cpp)
endif()

add_executable(PathOfPriceCheck ${APP_SOURCES})
target_include_directories(PathOfPriceCheck PRIVATE src)
target_compile_definitions(PathOfPriceCheck PRIVATE APP_VERSION="${APP_VERSION}" SDL_MAIN_HANDLED)
target_link_libraries(PathOfPriceCheck PRIVATE ppc_core SDL3::SDL3 imgui
                                               nlohmann_json::nlohmann_json CURL::libcurl)

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
  if(NOT TARGET X11::Xfixes)
    message(FATAL_ERROR "libXfixes (libxfixes-dev) is required to detect clipboard writes")
  endif()
  target_link_libraries(PathOfPriceCheck PRIVATE X11::X11 X11::Xtst X11::Xext X11::Xfixes)
endif()

enable_testing()

# Tests link ppc_core only, so they run headless in CI — no display, no network.
function(ppc_add_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE ppc_core doctest::doctest)
  # Fixtures live beside the sources so tests do not depend on the working directory.
  target_compile_definitions(${name} PRIVATE
    PPC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  # doctest 2.4.12 includes <ciso646>, which libstdc++ warns about under C++20.
  # Quoted: an unquoted '#' starts a CMake comment and eats the closing paren.
  if(NOT MSVC)
    target_compile_options(${name} PRIVATE "-Wno-#warnings")
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ppc_add_test(paths_test)
ppc_add_test(account_name_test)
ppc_add_test(leagues_test)
ppc_add_test(hash_test)
ppc_add_test(index_test)
ppc_add_test(normalize_test)
ppc_add_test(game_data_test)
ppc_add_test(stat_matcher_test)
ppc_add_test(install_test)
ppc_add_test(item_parse_test)
ppc_add_test(item_pricing_test)
