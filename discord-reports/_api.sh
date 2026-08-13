#!/usr/bin/env bash
# Shared Discord REST helper for ingest.sh and resolve.sh. Sourced after _env.sh, never run alone.
#
# api METHOD PATH [JSON_BODY] -> response body on stdout. A 429 is retried once after the delay
# Discord itself asks for; anything else >= 300 is printed to stderr and fails the call.
api() {
    local method=$1 path=$2 body=${3:-} resp code out
    if [[ -n $body ]]; then
        resp=$(curl -sS -w '\n%{http_code}' -X "$method" -H "$AUTH_HEADER" \
            -H 'Content-Type: application/json' -d "$body" "$DISCORD_API$path")
    else
        resp=$(curl -sS -w '\n%{http_code}' -X "$method" -H "$AUTH_HEADER" "$DISCORD_API$path")
    fi
    code=${resp##*$'\n'}
    out=${resp%$'\n'*}
    if [[ $code == 429 ]]; then
        sleep "$(jq -r '.retry_after // 1' <<<"$out")"
        api "$method" "$path" "$body"
        return
    fi
    if [[ $code -ge 300 ]]; then
        echo "Discord API $method $path -> $code: $out" >&2
        return 1
    fi
    printf '%s' "$out"
}
