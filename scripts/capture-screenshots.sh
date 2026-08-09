#!/usr/bin/env bash
#
# Re-captures the website's screenshots into `site/img/`.
#
# The overlay is drawn on a **virtual X display**, not on yours: nothing appears on screen, the
# system-wide hotkey grabs land on `:99` where they bother nobody, and the background is black
# everywhere the overlay did not paint, so trimming gives the panel and the item beside it and
# nothing else. `PPC_DEV_ITEM` is what opens the panel on a captured clipboard with no game
# running — see the dev environment variables in docs/architecture.md.
#
# **It runs real trade searches**, one per item, against the account's own rate limit — that is
# the point, since a screenshot of an empty results table sells nothing. `auto_search` and
# `debug_log` are overridden for the run and the config is put back afterwards, so what the
# Settings shot shows is what a new user sees rather than this machine's state.
#
# Needs: Xvfb, ImageMagick, a Release/Debug build in `build/`, an installed data bundle, and a
# league whose market has listings in it.

set -u
cd "$(dirname "$0")/.."

DISP=:99
GEOM=2560x1440x24
HEIGHT=980 # every shot is cropped to this so the gallery reads as one set
SETTLE=16  # data bundle, poe.ninja overview, exchange digest, trade search, CDN icons
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/PathOfPriceCheck/config.json"

# name | item file | hover x,y on the virtual screen (optional)
#
# The hover coordinate is what makes the seller's item appear beside a listing row, which has no
# other way of being photographed. It is in *screen* space, so it moves whenever the panel width
# or the screen geometry does — re-derive it from a first capture rather than trusting it.
SHOTS=(
    "panel-rare|tests/data/examples/item_6.txt|200,1300"
    "unique-reference|tests/data/examples/item_5.txt|"
    "listing-hover|tests/data/examples/item_5.txt|1323,486"
    "map|tests/data/items/map-rare-t16-corrupted.txt|"
    "currency-exchange|tests/data/items/currency-chaos-stack.txt|"
    "unidentified-picker|tests/data/items/unique-unidentified-jewel.txt|"
    "settings||200,1300" # no item: PPC_DEV_OVERLAY alone opens Settings
)

die() { printf 'capture: %s\n' "$1" >&2; exit 1; }
for t in Xvfb magick jq cc; do command -v $t >/dev/null || die "needs $t"; done
[ -x build/PathOfPriceCheck ] || die "no build/PathOfPriceCheck — build first"
[ -f "$CONFIG" ] || die "no config at $CONFIG — run the app once and pick a league"

work=$(mktemp -d)
restore() {
    [ -f "$work/config.json" ] && cp "$work/config.json" "$CONFIG"
    # Never `pkill -f 'Xvfb :99'`: -f matches this script's own command line and kills the shell.
    for p in $(pgrep -x Xvfb); do kill "$p" 2>/dev/null; done
    rm -rf "$work"
}
trap restore EXIT

cp "$CONFIG" "$work/config.json"
jq '.auto_search = true | .debug_log = false' "$work/config.json" >"$CONFIG"

# A plain XWarpPointer moves the pointer without generating the motion a widget reacts to, so
# hovering has to go through XTest — the same extension the app's own copy injection uses.
cat >"$work/warp.c" <<'EOF'
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    Display* d = XOpenDisplay(argv[1]);
    if (!d) return 1;
    int x = atoi(argv[2]), y = atoi(argv[3]);
    XTestFakeMotionEvent(d, 0, x - 8, y - 8, 0); XFlush(d); usleep(120000);
    XTestFakeMotionEvent(d, 0, x, y, 0);         XFlush(d); usleep(120000);
    XTestFakeMotionEvent(d, 0, x, y + 1, 0);     XFlush(d);
    XCloseDisplay(d);
    return 0;
}
EOF
cc -O1 -o "$work/warp" "$work/warp.c" -lX11 -lXtst || die "cannot build the pointer helper"

mkdir -p site/img
for s in "${SHOTS[@]}"; do
    IFS='|' read -r name item hover <<<"$s"
    printf '  %-22s' "$name"

    for p in $(pgrep -x Xvfb); do kill "$p" 2>/dev/null; done
    sleep 0.5
    Xvfb $DISP -screen 0 $GEOM -nolisten tcp >"$work/xvfb.log" 2>&1 &
    sleep 1.5

    if [ -n "$item" ]; then
        DISPLAY=$DISP PPC_DEV_OVERLAY=1 PPC_DEV_ITEM="$item" \
            ./build/PathOfPriceCheck >"$work/app.log" 2>&1 &
    else
        DISPLAY=$DISP PPC_DEV_OVERLAY=1 ./build/PathOfPriceCheck >"$work/app.log" 2>&1 &
    fi
    app=$!
    sleep $SETTLE

    if [ -n "$hover" ]; then
        "$work/warp" $DISP "${hover%,*}" "${hover#*,}"
        sleep 2.5
    fi

    DISPLAY=$DISP import -window root "$work/raw.png" 2>/dev/null
    kill $app 2>/dev/null; sleep 1; kill -9 $app 2>/dev/null
    for p in $(pgrep -x Xvfb); do kill "$p" 2>/dev/null; done

    magick "$work/raw.png" -trim +repage -crop "x${HEIGHT}+0+0" +repage "site/img/$name.png"
    identify -format '%wx%h\n' "site/img/$name.png"
done

echo "capture: site/img/ updated — check each one before committing (a listing carries the"
echo "         seller's handle, and your own is tinted green if account_name is set)"
