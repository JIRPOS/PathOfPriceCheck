#!/usr/bin/env bash
# Loads discord-reports/.env for ingest.sh and resolve.sh. Sourced, never run on its own. Callers
# are expected to have already `cd`ed into discord-reports/.
#
# Tracing is suspended across the read and restored afterwards, same reason as worker/_env.sh:
# `bash -x` would otherwise print the bot token to the terminal.

_ppc_xtrace=$(set +o | grep -E 'xtrace$')
set +x

if [[ ! -f .env ]]; then
    echo "discord-reports/.env is missing — copy .env.example and fill it in (see README.md)" >&2
    exit 1
fi

set -a
# shellcheck disable=SC1091
. ./.env
set +a

if [[ -z ${DISCORD_BOT_TOKEN:-} ]]; then
    echo "DISCORD_BOT_TOKEN is not set in discord-reports/.env" >&2
    exit 1
fi
if [[ -z ${DISCORD_GUILD_ID:-} ]]; then
    echo "DISCORD_GUILD_ID is not set in discord-reports/.env" >&2
    exit 1
fi
if [[ -z ${DISCORD_CHANNEL_ID:-} ]]; then
    echo "DISCORD_CHANNEL_ID is not set in discord-reports/.env" >&2
    exit 1
fi

DISCORD_API="https://discord.com/api/v10"
AUTH_HEADER="Authorization: Bot $DISCORD_BOT_TOKEN"

eval "$_ppc_xtrace"
unset _ppc_xtrace
