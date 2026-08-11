# Loads worker/.env for the deploy scripts. Sourced, never run on its own.
#
# Tracing is suspended across the read and restored afterwards: `bash -x ./publish.sh` would
# otherwise print the API token to the terminal, which is how a local-only credential stops being
# local-only. Everything else in these scripts is safe to trace, and worth tracing.

_ppc_xtrace=$(set +o | grep -E 'xtrace$')
set +x

if [[ ! -f .env ]]; then
    echo "worker/.env is missing — copy .env.example and fill it in (see README.md)" >&2
    exit 1
fi

set -a
# shellcheck disable=SC1091
. ./.env
set +a

# Every test below expands a credential, and an expansion is what a trace prints — so the checks
# have to happen while tracing is still off, not just the read.
if [[ -z ${CLOUDFLARE_API_TOKEN:-} ]]; then
    echo "CLOUDFLARE_API_TOKEN is not set in worker/.env" >&2
    exit 1
fi
if [[ -z ${CLOUDFLARE_ACCOUNT_ID:-} ]]; then
    echo "CLOUDFLARE_ACCOUNT_ID is not set in worker/.env" >&2
    exit 1
fi

if ! command -v npx >/dev/null; then
    echo "npx not found." >&2
    echo "node is installed through fnm, which puts it on PATH from a shell hook — a script" >&2
    echo "started without that hook will not see it. Run this from a shell where 'npx --version'" >&2
    echo "works, or point PATH at the fnm shim." >&2
    exit 1
fi

# wrangler asks about usage metrics the first time it runs and blocks on the answer, which looks
# exactly like a hang when the prompt is buried in script output.
export WRANGLER_SEND_METRICS=false

eval "$_ppc_xtrace"
unset _ppc_xtrace
