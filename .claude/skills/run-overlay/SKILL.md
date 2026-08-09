---
name: run-overlay
description: Build, launch and screenshot the PathOfPriceCheck overlay without Path of Exile running. Use when asked to run, build and run, launch, or show the app, to see a UI change working, or to capture the price-check panel, Settings or the idle marker for review or for the website.
---

# Running the overlay without the game

The app is an overlay and a tray icon — there is nothing to see on stdout. To check a change you
launch it, photograph it, and look. `PPC_DEV_ITEM` is what makes that possible with no game running.

## Environment variables

| Variable | Effect |
| --- | --- |
| `PPC_DEV_OVERLAY=1` | Opens Settings and disables dismiss-on-blur. Required for every dev run. |
| `PPC_DEV_ITEM=<file>` | Opens the price-check panel on a captured clipboard instead. |
| `PPC_DEV_IDLE=1` | Keeps the idle status marker up (it otherwise shows only while the game is in front). |
| `PPC_MANAGED=1` | Lets the window manager manage the window — needed if you want it stackable/movable on a real session. |
| `PPC_DEBUG_COPY=1` | Traces the copy timeline to stderr. |
| `PPC_FONT_DIR=<dir>` | Overrides the embedded Fontin faces with on-disk TTFs. |

## A quick look

```sh
cmake --build build -j
pkill -f PathOfPriceCheck            # one instance per user — a second launch refuses, loudly
(PPC_DEV_OVERLAY=1 PPC_DEV_ITEM=tests/data/items/<file>.txt \
  ./build/PathOfPriceCheck > "$SCRATCH/run.log" 2>&1 &)
sleep 10                             # data bundle, poe.ninja overview, exchange digest, CDN icons
spectacle -b -n -f -o "$SCRATCH/shot.png"
magick "$SCRATCH/shot.png" -crop 480x620+1220+0 +repage -resize 220% "$SCRATCH/panel.png"
```

Then `Read` the cropped PNG. Notes that matter:

- **Sleep long enough.** Under ten seconds the panel is drawn before its prices land and the
  screenshot shows a half-populated check. Sixteen is what the website script uses when a real trade
  search is involved.
- **Crop and upscale.** The panel is a narrow column on a 2560-wide screen; a full-screen shot is
  unreadable at review size.
- On this machine's Wayland session `spectacle -b -n -f -o` works; `import -window root` needs
  `DISPLAY=:0` and only sees Xwayland. Prefer the virtual display below for anything scripted.
- **Kill the instance afterwards.** The single-instance lock means a forgotten copy blocks the next
  run, and its hotkey grabs are system-wide.

## A clean capture, on a virtual display

`./scripts/capture-screenshots.sh` regenerates the website's gallery and is the worked example of
this. It runs the overlay on `Xvfb :99`, so nothing appears on the user's screen, the system-wide
hotkey grabs land where they bother nobody, and the screen is black everywhere the overlay did not
paint — which is what makes trimming to the panel alone possible. It also moves the pointer with
XTest (a plain `XWarpPointer` generates no motion a widget reacts to), which is the only way to
photograph a seller's item beside a hovered listing.

```sh
Xvfb :99 -screen 0 2560x1440x24 -nolisten tcp >/dev/null 2>&1 &
DISPLAY=:99 PPC_DEV_OVERLAY=1 PPC_DEV_ITEM=<file> ./build/PathOfPriceCheck &
sleep 10
DISPLAY=:99 import -window root shot.png
```

Note that it runs **real trade searches** against the maintainer's rate limit, one per item — do not
run it casually, and do not run it at all without asking.

## Before asking the user to test

Run the suite first — most of what a screenshot would show is covered headless:
`ctest --test-dir build`. The maintainer tests in game and reports back; a change to the copy path
or to hotkeys can only be verified that way. See the **clipboard-debug** skill for what to ask for
when they do.
