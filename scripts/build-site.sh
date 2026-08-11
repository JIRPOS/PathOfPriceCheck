#!/usr/bin/env bash
#
# Builds the project website into `_site/`.
#
#   ./scripts/build-site.sh              # build
#   python3 -m http.server -d _site      # preview on http://localhost:8000
#
# The root .md files are the single source of truth and are **never copied into `site/`**: each
# is rendered through GitHub's own `/markdown` endpoint, so a doc page reads exactly as the
# repository does and there is no site generator here to keep in step with one. The only
# hand-written pages are `site/index.html` and the wrapper `site/template.html`.
#
# `mode: markdown`, not `gfm`: the comment renderer turns every newline into a `<br>`, and every
# document in this repository is hard-wrapped prose.
#
# Needs `gh` (authenticated) and `jq`.

set -euo pipefail
cd "$(dirname "$0")/.."

REPO="${GH_REPO:-JIRPOS/PathOfPriceCheck}"
BASE_URL="${SITE_BASE_URL:-https://jirpos.github.io/PathOfPriceCheck}"
BLOB="https://github.com/$REPO/blob/master"
OUT="${1:-_site}"

# source | output | nav title
PAGES=(
    "README.md|about.html|About"
    "ROADMAP.md|roadmap.html|Roadmap"
    "PRIVACY.md|privacy.html|Privacy"
    "ATTRIBUTION.md|attribution.html|Attribution"
    "EULA.md|eula.html|Terms of use"
    "CONTACT.md|contact.html|Contact"
    "BUILDING.md|building.html|Building from source"
    "CONTRIBUTING.md|contributing.html|Contributing"
    "UNIQUE-MODS.md|unique-mods.html|Unique modifier data"
)

# file | headline | bullet ; bullet ; bullet
#
# Only the ones that exist are published, so the gallery is whatever has been captured so far
# rather than a row of broken images. No `|` or `;` inside the copy — they are the separators.
SHOTS=(
    "panel-rare.png|The Price Check Window|Just one hotkey away;Rebuilt item preview;Modifier parsing including Rank, Tier and Ranges;Pre-selected modifiers based on strategies, but you decide what to search by"
    "listing-hover.png|See what you are comparing to|Item previews straight from the live trade site results;Rendered exactly the way your own item is;See the asking price and the gold fee as a bonus;Whenever available, the going rate poe.ninja records, and which way it is heading with a week of history behind it;Recognises variable modifiers (can roll n of m modifiers)"
    "map.png|Every kind of item asks its own question|Different item, different strategy;Smart search for maps based on Tier, Quantity, More X modifiers…;Custom strategy for Blighted, Blight-ravaged, Corrupted or Influenced maps;Valdo map reward and Void matching"
    "currency-exchange.png|Faustus does the work, you should see it|See how an item tradeable on the currency exchange actually trades;See the average price, volume, maximums and minimums for the stated period;Straight from GGG, published once each hour has closed;Every hour ever published* is crawled, going all the way back to Settlers, so no item that has ever traded there is missed;* Only to know what trades there at all — the prices you see are always the single hour named under the table"
    "unidentified-picker.png|It knows what it cannot know|Identify an unidentified unique from a list;Pictures included! (unless they aren't);The trade site search matches the Identified/Unidentified status, so the listings are the same product you are holding;Careful: poe.ninja shows you the identified price — don't forget to search!;Not a gambler? Never turn a Watcher's Eye into a 5c item*;* Without checking what the unidentified ones are selling for first!"
    "report.png|Something priced wrong? Say so from where it happened|Bug reporting with zero friction;Sends what the tool actually used to identify the item, which is the thing a maintainer has to see;As anonymous as it can be made — every account name is scrubbed out of the trade results before the picture is taken;Everything you see is what will be sent. Nothing more. No fingerprint, no identifying information, just what matters for fixing the problem;A screenshot sometimes tells the rest of the story, so you get the preview and the choice of whether to attach it"
    "settings.png|Some Assembly Required|You should probably tell the tool the league you play in;Adjustable range matching including open-sided matching;Want a pleasant surprise? Set up your own account name;The tool will spot your own listings in the results and highlight them"
)

die() { printf 'build-site: %s\n' "$1" >&2; exit 1; }
command -v gh >/dev/null || die "needs the GitHub CLI (gh)"
command -v jq >/dev/null || die "needs jq"

rm -rf "$OUT"
mkdir -p "$OUT/img"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# --- the latest release, for the download buttons ------------------------------------------
#
# `releases/latest/download/<name>` cannot be used here: our asset names carry the version, so
# there is no fixed URL to link. The names are read from the release itself and baked in, which
# is also why publishing a release redeploys the site.

rel=$work/release.json
if ! gh release view --repo "$REPO" \
        --json tagName,publishedAt,assets >"$rel" 2>/dev/null; then
    echo "build-site: no published release; the download buttons will point at the releases page" >&2
    echo '{"tagName":"","publishedAt":"","assets":[]}' >"$rel"
fi

asset_url() { jq -r --arg m "$1" '[.assets[]|select(.name|test($m))][0].url // ""' "$rel"; }
asset_size() {
    local b; b=$(jq -r --arg m "$1" '[.assets[]|select(.name|test($m))][0].size // 0' "$rel")
    [ "$b" -gt 0 ] && awk -v b="$b" 'BEGIN{printf "%.1f MB", b/1048576}' || printf 'download'
}

VERSION=$(jq -r '.tagName' "$rel")
RELEASED=$(jq -r '.publishedAt' "$rel" | cut -dT -f1)
RELEASES_URL="https://github.com/$REPO/releases"
[ -n "$VERSION" ] || VERSION="unreleased"

DL_WIN_SETUP=$(asset_url 'win64-setup\.exe$'); [ -n "$DL_WIN_SETUP" ] || DL_WIN_SETUP=$RELEASES_URL
DL_WIN=$(asset_url 'win64\.zip$');           [ -n "$DL_WIN" ] || DL_WIN=$RELEASES_URL
DL_APPIMAGE=$(asset_url '\.AppImage$');      [ -n "$DL_APPIMAGE" ] || DL_APPIMAGE=$RELEASES_URL
DL_TAR=$(asset_url 'linux-x64\.tar\.gz$');   [ -n "$DL_TAR" ] || DL_TAR=$RELEASES_URL

# --- templating ----------------------------------------------------------------------------

esc() { printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'; }

# tpl <template> KEY VALUE ... — single-line values only.
tpl() {
    local f=$1; shift
    local args=()
    while [ $# -gt 0 ]; do args+=(-e "s|{{$1}}|$(esc "$2")|g"); shift 2; done
    sed "${args[@]}" "$f"
}

# inject <marker> <file> — replaces the marker's whole line with a file's contents.
inject() {
    awk -v marker="$1" -v file="$2" '
        index($0, marker) { while ((getline line < file) > 0) print line; next }
        { print }'
}

# The class is the page's own name, which is what carries the glyph: `.nav-<page>` in the
# stylesheet holds the icon, and a page listed here without a rule there simply goes without one.
nav=""
for p in "${PAGES[@]}"; do
    IFS='|' read -r _ out title <<<"$p"
    case $out in about.html|roadmap.html|privacy.html|attribution.html|contact.html) ;; *) continue ;; esac
    nav+="<a class=\"nav-${out%.html}\" href=\"$out\">$title</a>"
done
nav+="<a class=\"nav-github\" href=\"https://github.com/$REPO\">GitHub</a>"

docs_list=""
for p in "${PAGES[@]}"; do
    IFS='|' read -r _ out title <<<"$p"
    docs_list+="<li><a href=\"$out\">$title</a></li>"
done
docs_list+="<li><a href=\"license.html\">License</a></li>"

# --- render the documents ------------------------------------------------------------------
#
# Two link fixups, and both are load-bearing. A link to a published document becomes its page
# here; everything else a document points at — `VERSION`, `LICENSE`, anything under `src/` — is
# a file that exists only in the repository, and without the second rule every one of them 404s.

fix_links() {
    local args=()
    for p in "${PAGES[@]}"; do
        IFS='|' read -r src out _ <<<"$p"
        args+=(-e "s|href=\"$src|href=\"$out|g")
    done
    args+=(-e 's|href="LICENSE"|href="license.html"|g')
    # Every heading is `<a id="user-content-x" href="#x">`: the renderer namespaces the id but
    # not the link it writes for it, and fixes the two up with JavaScript on its own site. Left
    # alone, no in-page anchor resolves — which is every table of contents in these documents.
    args+=(-e 's|id="user-content-|id="|g')
    # Protect what is already a page, send the rest to the repository, unprotect.
    args+=(-e 's|href="\([a-z-]*\.html\)|href="@@\1|g')
    args+=(-e "s|href=\"\([^\":#/@][^\":]*\)\"|href=\"$BLOB/\1\"|g")
    args+=(-e 's|href="@@|href="|g')
    sed "${args[@]}"
}

render() { jq -n --rawfile text "$1" '{text:$text, mode:"markdown"}' | gh api --method POST /markdown --input -; }

page() { # page <title> <body-html-file> <output>
    tpl site/template.html \
        TITLE "$1" NAV "$nav" DOCS "$docs_list" REPO "$REPO" \
        VERSION "$VERSION" BASE_URL "$BASE_URL" \
        | inject '{{BODY}}' "$2" >"$OUT/$3"
}

for p in "${PAGES[@]}"; do
    IFS='|' read -r src out title <<<"$p"
    [ -f "$src" ] || die "missing $src"
    echo "  $src -> $out"
    render "$src" | fix_links >"$work/body.html"
    page "$title" "$work/body.html" "$out"
done

# The licence is not Markdown; a fence is what keeps its own line breaks.
{ printf '# License\n\n```\n'; cat LICENSE; printf '```\n'; } >"$work/license.md"
render "$work/license.md" | fix_links >"$work/body.html"
page "License" "$work/body.html" license.html

# --- the landing page ----------------------------------------------------------------------

# A scroll-snap strip rather than a stack: the captures are portraits, and a page is wider than
# it is tall — the same argument the panel itself makes about docking beside an item. Swiping,
# scrolling, arrow keys and the numbered links all come from the browser, and are the whole of
# it when `site/gallery.js` does not run — which is why nothing that script adds is emitted
# here: prev/next arrows, the rotation and the pop-out all need to know which slide is showing,
# and the markup cannot.
gallery=""
dots=""
og_image="popc_icon.png"
n=0
for s in "${SHOTS[@]}"; do
    IFS='|' read -r file head bullets <<<"$s"
    [ -f "site/img/$file" ] || continue
    n=$((n + 1))
    [ "$og_image" = "popc_icon.png" ] && og_image="img/$file"

    # Intrinsic size where it can be had, so a lazily-loaded slide reserves its space instead of
    # shifting the strip when it arrives.
    dim=""
    if command -v magick >/dev/null; then
        # The `\n` is not cosmetic: without it `read` hits EOF, returns non-zero, and `set -e`
        # takes the whole build down.
        read -r iw ih < <(magick identify -format '%w %h\n' "site/img/$file" 2>/dev/null) || true
        [ -n "${iw:-}" ] && dim=" width=\"$iw\" height=\"$ih\""
    fi
    lazy=" loading=\"lazy\""
    [ "$n" = 1 ] && lazy="" # the first slide is above the fold; lazily loading it only delays it

    items=""
    IFS=';' read -ra parts <<<"$bullets"
    for b in "${parts[@]}"; do items+="<li>$b</li>"; done

    gallery+="<figure class=\"slide\" id=\"shot-$n\">"
    gallery+="<a href=\"img/$file\"><img src=\"img/$file\" alt=\"$head\"$dim$lazy></a>"
    gallery+="<figcaption><h3>$head</h3><ul>$items</ul></figcaption></figure>"
    dots+="<a href=\"#shot-$n\">$n</a>"
done
if [ -n "$gallery" ]; then
    {
        printf '<section class="shots" id="screenshots">\n'
        printf '<h2>Gallery</h2>\n'
        printf '<div class="strip" tabindex="0" role="region" aria-label="Screenshots">%s</div>\n' "$gallery"
        printf '<nav class="dots" aria-label="Jump to a screenshot"><span>Scroll, swipe or pick one</span>%s</nav>\n' "$dots"
        printf '</section>\n'
    } >"$work/gallery.html"
else
    echo "build-site: no screenshots in site/img/, omitting the gallery" >&2
    : >"$work/gallery.html"
fi

tpl site/index.html \
    NAV "$nav" DOCS "$docs_list" REPO "$REPO" BASE_URL "$BASE_URL" \
    VERSION "$VERSION" RELEASED "$RELEASED" RELEASES_URL "$RELEASES_URL" \
    OG_IMAGE "$og_image" \
    DL_WIN_SETUP "$DL_WIN_SETUP" DL_WIN_SETUP_SIZE "$(asset_size 'win64-setup\.exe$')" \
    DL_WIN "$DL_WIN" DL_WIN_SIZE "$(asset_size 'win64\.zip$')" \
    DL_APPIMAGE "$DL_APPIMAGE" DL_APPIMAGE_SIZE "$(asset_size '\.AppImage$')" \
    DL_TAR "$DL_TAR" DL_TAR_SIZE "$(asset_size 'linux-x64\.tar\.gz$')" \
    | inject '{{GALLERY}}' "$work/gallery.html" >"$OUT/index.html"

# --- static ---------------------------------------------------------------------------------

cp site/style.css site/gallery.js "$OUT/"
cp assets/popc_icon.ico "$OUT/favicon.ico"
cp assets/popc_icon.png "$OUT/popc_icon.png"
for f in site/img/*.png site/img/*.jpg; do
    if [ -f "$f" ]; then cp "$f" "$OUT/img/"; fi
done
rmdir "$OUT/img" 2>/dev/null || true # empty until the first screenshot lands

{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n'
    printf '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
    for f in "$OUT"/*.html; do
        printf '  <url><loc>%s/%s</loc></url>\n' "$BASE_URL" "$(basename "$f")"
    done
    printf '</urlset>\n'
} >"$OUT/sitemap.xml"

printf 'User-agent: *\nAllow: /\nSitemap: %s/sitemap.xml\n' "$BASE_URL" >"$OUT/robots.txt"

echo "build-site: $OUT ready ($VERSION)"
