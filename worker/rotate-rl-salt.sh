#!/usr/bin/env bash
# Set or replace the HMAC key the rate limiter derives its per-IP counter keys from.
#
# Nobody needs to know this value, so unlike the webhook it is generated here and never shown: it
# goes from /dev/urandom to Cloudflare's secret store and is not echoed, not written to disk, and not
# recoverable. Running this again forgets the hour's counters, which is the only thing it costs.
set -euo pipefail

cd "$(dirname "$0")"
# shellcheck source=_env.sh
. ./_env.sh

set +x # the salt is a credential; keep it out of a trace for the same reason .env is
openssl rand -hex 32 | tr -d '\n' | npx --yes wrangler secret put RL_SALT
