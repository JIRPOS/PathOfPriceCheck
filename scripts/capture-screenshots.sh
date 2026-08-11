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
HEIGHT=980 # the tallest a shot may be, so no one slide towers over the gallery
SETTLE=16  # data bundle, poe.ninja overview, exchange digest, trade search, CDN icons
PARK=20,1400 # somewhere no window is, so nothing is left hovered when the shutter goes
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/PathOfPriceCheck/config.json"

# The update check is pointed at a closed port for the run: this build is whatever is in the tree
# and is therefore usually behind the published release, and a screenshot selling the tool should
# not open on its own "a new version is out" banner. A failed check is silent by design.
NOUPDATE=http://127.0.0.1:9/latest.json

# name | item file | hover x,y on the virtual screen (optional)
#
# The hover coordinate is what makes the seller's item appear beside a listing row, which has no
# other way of being photographed. It is in *screen* space, so it moves whenever the panel width
# or the screen geometry does — re-derive it from a first capture rather than trusting it.
# A shot that needs more than one pointer move says so in `stage` below instead.
SHOTS=(
    "panel-rare|tests/data/examples/item_6.txt|200,1300"
    "listing-hover|tests/data/examples/item_5.txt|1323,486"
    "map|tests/data/items/map-rare-t16-corrupted.txt|"
    "currency-exchange|tests/data/items/currency-chaos-stack.txt|"
    "unidentified-picker|tests/data/items/unique-unidentified-jewel.txt|"
    "report|tests/data/examples/item_5.txt|"
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

# `account_name` is blanked for the run because the Settings shot draws the field: what belongs on
# the published picture is the placeholder a new user sees, not the maintainer's handle. Sellers'
# handles are somebody else's name again and are masked by `PPC_DEV_ANON` below.
cp "$CONFIG" "$work/config.json"
jq '.auto_search = true | .debug_log = false | .account_name = ""' "$work/config.json" >"$CONFIG"

# A plain XWarpPointer moves the pointer without generating the motion a widget reacts to, so
# hovering has to go through XTest — the same extension the app's own copy injection uses. The
# same helper clicks and types, which is how a shot of a dialog gets taken at all: there is no
# hotkey onto the bug reporter, and a report with an empty description photographs as a form
# nobody filled in.
#
#   xdo <display> move <x> <y>          xdo <display> click <x> <y>       xdo <display> type <text>
cat >"$work/xdo.c" <<'EOF'
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void move(Display* d, int x, int y) {
    XTestFakeMotionEvent(d, 0, x - 8, y - 8, 0); XFlush(d); usleep(120000);
    XTestFakeMotionEvent(d, 0, x, y, 0);         XFlush(d); usleep(120000);
    XTestFakeMotionEvent(d, 0, x, y + 1, 0);     XFlush(d); usleep(120000);
}

// ASCII is its own keysym over the printable range, so the layout lookup is the whole of it:
// find the keycode carrying that keysym and note whether it sits in the shifted slot.
static void type_char(Display* d, char c) {
    KeySym ks = (c == '\n') ? XK_Return : (KeySym)(unsigned char)c;
    KeyCode kc = XKeysymToKeycode(d, ks);
    if (!kc) return;
    int shift = XkbKeycodeToKeysym(d, kc, 0, 0) != ks && XkbKeycodeToKeysym(d, kc, 0, 1) == ks;
    KeyCode sh = XKeysymToKeycode(d, XK_Shift_L);
    if (shift) XTestFakeKeyEvent(d, sh, True, 0);
    XTestFakeKeyEvent(d, kc, True, 0);
    XTestFakeKeyEvent(d, kc, False, 0);
    if (shift) XTestFakeKeyEvent(d, sh, False, 0);
    XFlush(d);
    usleep(12000);
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    Display* d = XOpenDisplay(argv[1]);
    if (!d) return 1;
    const char* what = argv[2];
    if (!strcmp(what, "type")) {
        for (const char* p = argv[3]; *p; ++p) type_char(d, *p);
    } else {
        if (argc < 5) return 2;
        move(d, atoi(argv[3]), atoi(argv[4]));
        if (!strcmp(what, "click")) {
            XTestFakeButtonEvent(d, 1, True, 0);  XFlush(d); usleep(80000);
            XTestFakeButtonEvent(d, 1, False, 0); XFlush(d);
        }
    }
    XCloseDisplay(d);
    return 0;
}
EOF
cc -O1 -o "$work/xdo" "$work/xdo.c" -lX11 -lXtst || die "cannot build the pointer helper"

move() { "$work/xdo" $DISP move "${1%,*}" "${1#*,}"; }
click() { "$work/xdo" $DISP click "${1%,*}" "${1#*,}"; sleep "${2:-1}"; }
type_in() { "$work/xdo" $DISP type "$1"; sleep 0.5; }

# Everything a shot needs on screen beyond opening on an item. Coordinates are the virtual
# screen's, and the dialog ones are stable because it is a fixed size centred on it — 940x660 at
# 810,390 — so they only move when that size does.
#
# The report shot is the one that is *staged* rather than photographed as found: a description
# typed in, and the consent box ticked so the picture shows what ticking it attaches. Everything
# the dialog draws is still the app's own — the masked seller names in the preview included.
stage() {
    case $1 in
    report)
        click 1590,368 2 # the report glyph on the results toolbar, which opens the dialog
        click 1210,490   # into the description box
        type_in "The reference row says 142 chaos but every listing is 2 divine - is it reading the wrong variant?"
        click 1315,1022  # tick "Attach the screenshot"
        ;;
    *) return 0 ;;
    esac
    # Parked afterwards: the pointer is sitting on whatever it last pressed, and a tooltip that
    # happens to be up is a picture of the click rather than of the dialog.
    move "$PARK"
    sleep 1.5
}

mkdir -p site/img
for s in "${SHOTS[@]}"; do
    IFS='|' read -r name item hover <<<"$s"
    printf '  %-22s' "$name"

    for p in $(pgrep -x Xvfb); do kill "$p" 2>/dev/null; done
    sleep 0.5
    Xvfb $DISP -screen 0 $GEOM -nolisten tcp >"$work/xvfb.log" 2>&1 &
    sleep 1.5

    vars=(DISPLAY=$DISP PPC_DEV_OVERLAY=1 PPC_DEV_UPDATE_URL=$NOUPDATE PPC_DEV_ANON=1)
    [ -n "$item" ] && vars+=(PPC_DEV_ITEM="$item") # without one, PPC_DEV_OVERLAY opens Settings
    env "${vars[@]}" ./build/PathOfPriceCheck >"$work/app.log" 2>&1 &
    app=$!
    sleep $SETTLE

    if [ -n "$hover" ]; then
        move "$hover"
        sleep 2.5
    fi
    stage "$name"

    DISPLAY=$DISP import -window root "$work/raw.png" 2>/dev/null
    kill $app 2>/dev/null; sleep 1; kill -9 $app 2>/dev/null
    for p in $(pgrep -x Xvfb); do kill "$p" 2>/dev/null; done

    magick "$work/raw.png" -trim +repage -crop "x${HEIGHT}+0+0" +repage "site/img/$name.png"
    identify -format '%wx%h\n' "site/img/$name.png"
done

echo "capture: site/img/ updated — check each one before committing. Handles are masked by"
echo "         PPC_DEV_ANON and the account field is blanked, so what to look for is a name"
echo "         that got through anyway, and anything else on screen that names this machine."
