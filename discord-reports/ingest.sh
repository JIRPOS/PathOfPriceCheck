#!/usr/bin/env bash
# Pull new reports out of the #ppc-reports Discord forum into inbox/, one folder per thread, ready
# for something else — a human or another agent — to investigate. Ingestion does no investigation
# of its own; see .claude/skills/discord-reports-ingest/SKILL.md.
#
# "Already ingested" is tracked with a reaction on the thread's starter message, not a local state
# file, so re-running after wiping inbox/ re-downloads instead of silently skipping everything —
# see README.md.
set -euo pipefail

cd "$(dirname "$0")"
# shellcheck source=_env.sh
. ./_env.sh
# shellcheck source=_api.sh
. ./_api.sh

MARK='📥'
MARK_ENC='%F0%9F%93%A5' # percent-encoded UTF-8 for the reactions endpoint
mkdir -p inbox

slugify() {
    tr '[:upper:]' '[:lower:]' <<<"$1" | sed -E 's/[^a-z0-9]+/-/g; s/^-+|-+$//g' | cut -c1-60
}

threads_json=$(mktemp)
trap 'rm -f "$threads_json"' EXIT

# Active threads are listed at the guild level (not the channel) since a Discord API change;
# archived ones are still channel-scoped and paginated 100 at a time, oldest cut off first.
api GET "/guilds/$DISCORD_GUILD_ID/threads/active" \
    | jq -c --arg cid "$DISCORD_CHANNEL_ID" \
        '.threads[] | select(.parent_id == $cid) | {id, name, applied_tags}' \
    >>"$threads_json"

before=""
while :; do
    page=$(api GET "/channels/$DISCORD_CHANNEL_ID/threads/archived/public?limit=100${before:+&before=$before}")
    jq -c '.threads[] | {id, name, applied_tags}' <<<"$page" >>"$threads_json"
    [[ $(jq -r '.has_more' <<<"$page") == "true" ]] || break
    before=$(jq -r '[.threads[].thread_metadata.archive_timestamp] | min' <<<"$page")
done

tag_map=$(api GET "/channels/$DISCORD_CHANNEL_ID" | jq -c '[.available_tags[] | {(.id): .name}] | add // {}')

ingested=0
skipped=0
while IFS= read -r thread; do
    id=$(jq -r '.id' <<<"$thread")
    name=$(jq -r '.name' <<<"$thread")

    msg=$(api GET "/channels/$id/messages/$id") || continue
    already=$(jq -r --arg e "$MARK" '[.reactions[]? | select(.emoji.name == $e) | .me] | any' <<<"$msg")
    if [[ $already == "true" ]]; then
        skipped=$((skipped + 1))
        continue
    fi

    report_url=$(jq -r '.attachments[]? | select(.filename == "report.md") | .url' <<<"$msg")
    if [[ -z $report_url ]]; then
        echo "thread $id ($name) has no report.md attachment, skipping" >&2
        continue
    fi
    # A screenshot rides as the embed's image, not a plain attachment: the worker uploads it and
    # points embed.image at it via attachment://, which folds it into the embed instead of leaving
    # it in .attachments (see worker/src/index.js).
    shot_url=$(jq -r '.embeds[0].image.url // empty' <<<"$msg")

    dir="inbox/${id}_$(slugify "$name")"
    mkdir -p "$dir"
    curl -sS -o "$dir/report.md" "$report_url"
    [[ -n $shot_url ]] && curl -sS -o "$dir/screenshot.png" "$shot_url"

    tags_json=$(jq -c --argjson m "$tag_map" '[.applied_tags[]? as $t | ($m[$t] // $t)]' <<<"$thread")

    jq -n \
        --arg thread_id "$id" \
        --arg guild_id "$DISCORD_GUILD_ID" \
        --arg channel_id "$DISCORD_CHANNEL_ID" \
        --arg title "$name" \
        --arg url "https://discord.com/channels/$DISCORD_GUILD_ID/$id" \
        --arg created_at "$(jq -r '.timestamp' <<<"$msg")" \
        --arg ingested_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        --argjson tags "$tags_json" \
        '{thread_id:$thread_id, guild_id:$guild_id, channel_id:$channel_id, title:$title, url:$url,
          tags:$tags, created_at:$created_at, ingested_at:$ingested_at,
          status:"new", resolution:null, closed_at:null}' \
        >"$dir/meta.json"

    api PUT "/channels/$id/messages/$id/reactions/$MARK_ENC/@me" >/dev/null

    echo "ingested $id -> $dir"
    ingested=$((ingested + 1))
done <"$threads_json"

echo "$ingested new, $skipped already ingested"
