#!/usr/bin/env bash
# Set or replace the Discord webhook the relay posts to.
#
# The URL is read from the terminal and handed straight to Cloudflare. It is deliberately not in
# .env, not in this repo, and not in any file: rotating it means deleting the webhook in Discord,
# making a new one, and running this. Clients never notice — they only ever knew the relay's URL.
set -euo pipefail

cd "$(dirname "$0")"
# shellcheck source=_env.sh
. ./_env.sh

echo "Paste the Discord webhook URL, then press Enter."
echo "It is not echoed and not written to disk."

set +x # the URL is a credential; keep it out of a trace for the same reason .env is
read -rs -p "> " webhook
echo

case "$webhook" in
    https://discord.com/api/webhooks/* | https://discordapp.com/api/webhooks/*) ;;
    *)
        echo "that is not a Discord webhook URL — expected https://discord.com/api/webhooks/..." >&2
        exit 1
        ;;
esac

printf '%s' "$webhook" | npx --yes wrangler secret put DISCORD_WEBHOOK
