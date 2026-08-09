#!/usr/bin/env bash
# Fetch Font Awesome Free and subset it down to the handful of UI glyphs the overlay
# draws, into assets/fonts/PPCGlyphs.ttf. Not vendored: like Fontin, the .ttf here is
# only an input to scripts/gen-glyph-data.sh — see assets/fonts/README.md.
#
# **The codepoint list below is the contract with src/ui/glyphs.hpp.** A glyph named
# there and missing here bakes as nothing at all, which is the failure mode Fontin's
# ≤ and ≥ already taught us: add to both, or to neither.
set -euo pipefail

ver="6.7.2"
url="https://github.com/FortAwesome/Font-Awesome/releases/download/$ver/fontawesome-free-$ver-web.zip"
# f00c check (confirm), f0e2 arrow-rotate-left (reset).
codepoints="U+F00C,U+F0E2"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$root/assets/fonts"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

command -v pyftsubset >/dev/null || {
  echo "pyftsubset not found — install fonttools (pip install fonttools)" >&2
  exit 1
}

echo "fetching $url"
curl -fsSL --retry 3 -o "$tmp/fa.zip" "$url"
unzip -q -o -j "$tmp/fa.zip" '*/webfonts/fa-solid-900.ttf' '*/LICENSE.txt' -d "$tmp"

mkdir -p "$dest"
# The web release's TrueType build, not the desktop release's OTF: ImGui rasterizes with
# stb_truetype, whose CFF path is the weaker one. Same reasoning as Fontin.
pyftsubset "$tmp/fa-solid-900.ttf" \
  --unicodes="$codepoints" \
  --output-file="$dest/PPCGlyphs.ttf" \
  --no-hinting --notdef-outline --drop-tables+=DSIG
cp "$tmp/LICENSE.txt" "$dest/FontAwesome-LICENSE.txt"

echo "installed $dest/PPCGlyphs.ttf ($(wc -c < "$dest/PPCGlyphs.ttf") bytes) for $codepoints"
