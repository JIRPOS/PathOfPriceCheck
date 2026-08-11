#!/usr/bin/env bash
# Deploy the report relay. Needs worker/.env, which is gitignored and exists only on the machine
# that is allowed to deploy — that is the whole access control, and it is why no workflow in this
# repo or the data repo may ever call this script.
set -euo pipefail

cd "$(dirname "$0")"
# shellcheck source=_env.sh
. ./_env.sh

npx --yes wrangler deploy "$@"

echo
echo "Deployed. The secret is separate — if this is a first deploy, set it now:"
echo "  ./rotate-webhook.sh"
