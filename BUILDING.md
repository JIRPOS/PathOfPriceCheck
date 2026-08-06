# Building

There is no dependency manager to set up and nothing to vendor by hand: CMake's `FetchContent`
clones and builds SDL3, Dear ImGui, nlohmann/json and doctest from pinned tags in
[CMakeLists.txt](CMakeLists.txt). What the system has to provide is a compiler, CMake, git, and —
on Linux — the development headers those libraries compile against.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

The **first** configure clones and builds SDL3, which takes several minutes and about a gigabyte
of build tree. Every configure after that is cached.

## Common prerequisites

| | minimum |
|---|---|
| CMake | 3.20 (4.x works) |
| Compiler | GCC 12+, Clang 15+, or MSVC 19.3x (Visual Studio 2022) — C++20 |
| git | any; `FetchContent` shells out to it |
| Network | needed at configure time, for the dependency clones |

**libcurl is the one dependency taken from the system where there is one.** `find_package(CURL)`
decides: on Linux it finds the distro package listed below, and on Windows it finds nothing, so
curl and zlib are fetched and built statically against Schannel. That is deliberate — it keeps the
Windows release a single `.exe` with no DLL beside it and no CA bundle to ship.

## Linux

X11 only. See [Runtime requirements](#runtime-requirements) below before building — a Wayland
session will build the binary fine and then not let it work.

**The `apt` list is the one CI installs on every push and pull request, so it is the only one
continuously verified.** The others are its equivalents and are checked by hand; if one is wrong,
the CMake error names the header it could not find, and an issue about it is welcome.

### Arch — and CachyOS, EndeavourOS, Manjaro

```sh
sudo pacman -S --needed base-devel cmake git \
  libx11 libxext libxrandr libxcursor libxi libxfixes libxss libxtst libxrender libxinerama \
  libxkbcommon wayland wayland-protocols libglvnd alsa-lib libpulse dbus systemd-libs curl
```

Arch ships headers in the runtime package, so there is no `-dev` split to chase. Two that are not
where the Debian names suggest: `libudev.h` comes from **`systemd-libs`**, and `GL/gl.h` /
`EGL/egl.h` from **`libglvnd`**.

There is **no AUR package**. The only artifacts published by this project are the GitHub releases;
anything on the AUR is somebody else's packaging and is not vouched for here.

### Debian, Ubuntu, Linux Mint, Pop!_OS

```sh
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake git \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxtst-dev libxrender-dev libxinerama-dev \
  libcurl4-openssl-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev
```

`apt` and `apt-get` take the same package names; use whichever your distribution prefers. Mint and
Pop!\_OS track the Ubuntu release they are built on, so the names are identical.

Version floors worth knowing: **Ubuntu 22.04** ships GCC 11 — install `g++-12` and configure with
`-DCMAKE_CXX_COMPILER=g++-12` if the build rejects C++20 constructs. **Debian 12** (bookworm, CMake
3.25 / GCC 12) is fine as shipped.

### Fedora

```sh
sudo dnf install -y gcc-c++ cmake git \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel \
  libXScrnSaver-devel libXtst-devel libXrender-devel libXinerama-devel \
  libcurl-devel libxkbcommon-devel wayland-devel wayland-protocols-devel \
  mesa-libGL-devel mesa-libEGL-devel alsa-lib-devel pulseaudio-libs-devel dbus-devel systemd-devel
```

If a name has drifted, `sudo dnf builddep SDL3` pulls what SDL itself needs, which is most of this
list; the rest is libcurl and the X11 extensions.

### SteamOS / Steam Deck

Untested, and two things are in the way rather than one. The root filesystem is read-only, so the
headers above cannot be installed onto it — build inside a container (`distrobox`, `toolbox`) or
just use the release tarball. Beyond that, the Deck's gaming session is gamescope rather than a
plain X11 session, and the global hotkey grab and overlay have not been tried against it.

### Other distributions

Nothing here is exotic: the list is X11 client libraries plus their `Xtst`/`Xfixes`/`Xext`
extensions, libcurl, and SDL3's own build dependencies (xkbcommon, wayland, Mesa GL/EGL, ALSA,
PulseAudio, D-Bus, udev). A missing one fails at configure time naming the header.

## Windows

Visual Studio 2022 with the **Desktop development with C++** workload, or the standalone Build
Tools, which bring MSVC, the Windows SDK, CMake and Ninja.

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Git.Git
winget install Kitware.CMake   # only if you did not get CMake from the workload
```

Then, from a **x64 Native Tools Command Prompt for VS 2022** (or any shell with CMake and MSVC on
`PATH`):

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

The binary lands at `build\Release\PathOfPriceCheck.exe` and is self-contained: curl, zlib, SDL3
and the fonts are all linked in, and TLS goes through Schannel, so there is no OpenSSL and no CA
bundle to install. It is a **GUI-subsystem** executable — nothing is printed to a console, because
a console-subsystem build would pop a window beside an application whose whole UI is an overlay
and a tray icon. Diagnostics go to the debug log instead (see below).

MSVC is what CI builds and is therefore what is known to work; clang-cl is untested.

## Runtime requirements

- **Linux: an X11 session.** Global hotkeys (`XGrabKey`), foreground-window detection, synthetic
  input (`XTest`) and clipboard ownership tracking (`XFixes`) are all X11. Wayland blocks arbitrary
  global hotkeys and click-through overlays without compositor portals or evdev access, and is a
  later stretch goal rather than something to work around — under a Wayland session the sensible
  answer today is to log into an X11 one. There is a known, unfixable-from-here failure where
  KWin's Xwayland clipboard bridge drops the selection owner entirely; see
  [CLAUDE.md](CLAUDE.md#architecture).
- **The release tarball is built on the CI Ubuntu image**, so it links that image's glibc and
  system libcurl. On an older distribution, build from source rather than fighting the loader.
- **Windows 10 or later.** No runtime dependencies beyond the OS.
- **The game may be the native Windows client or Wine/Proton.** The copy path carries a good deal
  of hard-won handling for how Wine publishes the clipboard; that story is in
  [CLAUDE.md](CLAUDE.md).
- **First launch downloads the data bundle** (~4 MB) from
  [PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data). Nothing is baked into
  the binary, so a new league needs a data build rather than a new release. See
  [PRIVACY.md](PRIVACY.md) for everything the application talks to.

## Running it

```sh
./build/PathOfPriceCheck
```

Development environment variables, for iterating without the game running:

| variable | effect |
|---|---|
| `PPC_DEV_OVERLAY=1` | opens Settings on launch and disables dismiss-on-focus-loss |
| `PPC_DEV_ITEM=<file>` | with the above, opens the price-check panel on a captured clipboard text |
| `PPC_DEV_IDLE=1` | keeps the idle status marker visible even when the game is not in front |
| `PPC_DEBUG_COPY=1` | traces the copy/clipboard timeline to stderr |
| `PPC_FONT_DIR=<dir>` | replaces the embedded Fontin faces with TTFs from a directory |

Captures to feed `PPC_DEV_ITEM` live in [`tests/data/examples/`](tests/data/examples).

## Tests

```sh
ctest --test-dir build                 # all
ctest --test-dir build -R item_parse_test -V   # one, verbose
```

Tests link `ppc_core` only — the static library holding everything that needs neither a window nor
a network — so they run headless, offline, and are the fast way to work on the parser, the data
layer, the query builder and the rate limiter. `ppc_core` links no SDL3, no ImGui, no X11 and no
libcurl, and that rule is worth keeping.

## Sanitizers

Not wired into CMake; pass them by hand:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

## Regenerating embedded assets

Both are committed, so a normal build needs neither.

```sh
./scripts/fetch-fonts.sh      # downloads the Fontin TTFs into the gitignored assets/fonts/
./scripts/gen-font-data.sh    # rewrites src/fontin_data.inc
./scripts/gen-icon-data.sh    # rewrites src/icon_data.inc and assets/popc_icon.ico (needs ImageMagick)
```

The test fixtures under `tests/data/bundle/` are a slice of a real data release and are regenerated
by `./scripts/slice-test-bundle.py`, never edited by hand — the `.index.bin` files address the
ndjson by byte offset, so one stray byte silently shifts every record out from under every lookup.

## Versioning

`MAJOR.MINOR.BUILD`. `MAJOR.MINOR` lives in [VERSION](VERSION) and is bumped by the **Version
bump** workflow; `BUILD` is the cumulative CI run counter, injected as `-DAPP_BUILD=<n>`. A local
build with no `APP_BUILD` reports `.0`. Pushes to `master` publish a release for win64 and
linux-x64.
