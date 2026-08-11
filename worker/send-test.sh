#!/usr/bin/env bash
# Post a report to the relay, to check the whole path before the app can send one itself.
#
#   ./send-test.sh https://ppc-reports.<subdomain>.workers.dev
#   ./send-test.sh <url> ../tests/data/items/currency-essence.txt
#
# With no item file it sends a fixture from tests/data/items. Add --hostile to send what a
# malicious reporter would: mentions, a masked link, a fence break and a right-to-left override.
# Everything in the resulting Discord message should be inert, and nobody should be pinged.
set -euo pipefail

cd "$(dirname "$0")"

url="${1:-}"
if [[ -z "$url" ]]; then
    echo "usage: ./send-test.sh <relay-url> [item-file] [--hostile]" >&2
    exit 1
fi
shift

item_file="../tests/data/items/rare-bow-doom-song.txt"
hostile=0
for arg in "$@"; do
    case "$arg" in
        --hostile) hostile=1 ;;
        *) item_file="$arg" ;;
    esac
done

if [[ ! -f "$item_file" ]]; then
    item_file="$(find ../tests/data/items -name '*.txt' | sort | head -1)"
    echo "note: using $item_file" >&2
fi

python3 - "$url" "$item_file" "$hostile" <<'PY'
import json, sys, urllib.request, urllib.error

url, item_file, hostile = sys.argv[1], sys.argv[2], sys.argv[3] == "1"

# Kept as escapes rather than literals: a file carrying a real U+202E is unreadable in exactly the
# way this payload is meant to demonstrate.
HOSTILE = (
    "@everyone @here <@&1234567890>\n"
    "```\n"
    "[totally safe](https://example.invalid/x)\n"
    "\u202egnihton ees uoy\u202c\n"
    "a \u200bzero width space and a \x00 nul"
)

payload = {
    "item": open(item_file, encoding="utf-8").read(),
    "parse": "[item]   parsed: rarity=3 class='Bows' name='Doom Song' base='Spine Bow' 6 modifiers\n"
             "[item]   modifier 3 matched no stat record",
    "comment": HOSTILE if hostile else
               "The third modifier is read as a suffix but the game shows it as a prefix.",
    "meta": {"version": "0.6.17", "os": "linux", "league": "Standard", "bundle": "2026-08-01"},
}
req = urllib.request.Request(
    url.rstrip("/") + "/report",
    data=json.dumps(payload).encode(),
    headers={
        "content-type": "application/json",
        # Cloudflare's edge answers a scripting-library User-Agent with a 403 (error 1010)
        # before the Worker is ever reached. The app sends net::user_agent(); mirror it.
        "user-agent": "PathOfPriceCheck/send-test (+https://github.com/JIRPOS/PathOfPriceCheck)",
    },
    method="POST",
)
try:
    with urllib.request.urlopen(req) as r:
        print(r.status, r.read().decode())
except urllib.error.HTTPError as e:
    print(e.code, e.read().decode())
    sys.exit(1)
PY
