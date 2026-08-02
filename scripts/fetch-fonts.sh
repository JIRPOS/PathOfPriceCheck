#!/usr/bin/env bash
# Fetch Fontin (TrueType release) into assets/fonts/. Not vendored: the license
# forbids redistribution — see assets/fonts/README.md.
set -euo pipefail

url="http://www.exljbris.com/dl/fontin2_pc.zip"
dest="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/assets/fonts"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "fetching $url"
curl -fsSL --retry 3 -o "$tmp/fontin.zip" "$url"
unzip -q -o -j "$tmp/fontin.zip" '*.ttf' 'ReadMe.txt' -d "$tmp"

mkdir -p "$dest"
cp "$tmp"/Fontin-*.ttf "$dest/"
cp "$tmp/ReadMe.txt" "$dest/Fontin-LICENSE.txt"

echo "installed into $dest:"
ls -1 "$dest"/Fontin-*.ttf
