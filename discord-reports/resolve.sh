#!/usr/bin/env bash
# Close out reports that have been marked resolved locally, and optionally purge old closed local
# copies. See .claude/skills/discord-reports-resolve/SKILL.md.
#
#   ./resolve.sh                  archive+lock the Discord thread for every inbox/*/meta.json with
#                                  status "resolved", then flip that status to "closed". If
#                                  "resolution" names a commit hash that exists in this repo, a
#                                  "Fixed in commit ..." message with a GitHub link is posted to
#                                  the thread first.
#   ./resolve.sh --cleanup        also list local folders closed longer than DISCORD_REPORTS_CLEANUP_DAYS
#   ./resolve.sh --cleanup --yes  ...and actually delete them (Discord thread is never touched)
set -euo pipefail

cd "$(dirname "$0")"
# shellcheck source=_env.sh
. ./_env.sh
# shellcheck source=_api.sh
. ./_api.sh

do_cleanup=0
confirm=0
for arg in "$@"; do
    case "$arg" in
        --cleanup) do_cleanup=1 ;;
        --yes) confirm=1 ;;
        *)
            echo "unknown argument: $arg (expected --cleanup and/or --yes)" >&2
            exit 1
            ;;
    esac
done

CLEANUP_DAYS=${DISCORD_REPORTS_CLEANUP_DAYS:-90}

GITHUB_REPO="https://github.com/JIRPOS/PathOfPriceCheck"

closed=0
for dir in inbox/*/; do
    [[ -f "$dir/meta.json" ]] || continue
    [[ $(jq -r '.status' "$dir/meta.json") == "resolved" ]] || continue

    thread_id=$(jq -r '.thread_id' "$dir/meta.json")

    # If the resolution names a real commit in this repo, post it to the thread before archiving —
    # only a hash git can actually verify counts, never a bare guess out of the resolution text.
    resolution=$(jq -r '.resolution // empty' "$dir/meta.json")
    commit=""
    if [[ -n $resolution ]]; then
        for candidate in $(grep -oE '\b[0-9a-f]{7,40}\b' <<<"$resolution"); do
            commit=$(git -C .. rev-parse --verify --quiet "${candidate}^{commit}" 2>/dev/null) && break
            commit=""
        done
    fi
    if [[ -n $commit ]]; then
        note_body=$(jq -n --arg c "Fixed in commit \`${commit:0:7}\` — $GITHUB_REPO/commit/$commit" \
            '{content: $c}')
        api POST "/channels/$thread_id/messages" "$note_body" >/dev/null \
            || echo "failed to post fixed-in-commit message to $thread_id ($dir), archiving anyway" >&2
    fi

    if ! api PATCH "/channels/$thread_id" '{"archived":true,"locked":true}' >/dev/null; then
        echo "failed to archive thread $thread_id ($dir), leaving it marked resolved" >&2
        continue
    fi

    tmp=$(mktemp)
    jq --arg t "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '.status = "closed" | .closed_at = $t' \
        "$dir/meta.json" >"$tmp"
    mv "$tmp" "$dir/meta.json"

    echo "closed $thread_id -> $dir"
    closed=$((closed + 1))
done
echo "$closed thread(s) archived and locked"

if [[ $do_cleanup -eq 1 ]]; then
    purged=0
    now=$(date -u +%s)
    for dir in inbox/*/; do
        [[ -f "$dir/meta.json" ]] || continue
        [[ $(jq -r '.status' "$dir/meta.json") == "closed" ]] || continue
        closed_at=$(jq -r '.closed_at // empty' "$dir/meta.json")
        [[ -n $closed_at ]] || continue
        closed_ts=$(date -u -d "$closed_at" +%s 2>/dev/null) || continue
        age_days=$(((now - closed_ts) / 86400))
        ((age_days >= CLEANUP_DAYS)) || continue

        if [[ $confirm -eq 1 ]]; then
            echo "purging $dir (closed ${age_days}d ago; the Discord thread itself is untouched)"
            rm -rf "$dir"
            purged=$((purged + 1))
        else
            echo "would purge $dir (closed ${age_days}d ago) — rerun with --cleanup --yes to delete"
        fi
    done
    [[ $confirm -eq 1 ]] && echo "$purged local folder(s) purged"
fi
