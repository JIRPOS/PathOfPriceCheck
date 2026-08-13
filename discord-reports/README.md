# Discord report ingestion

A local-only maintainer tool: pulls bug reports out of the private `#ppc-reports` Discord forum
(see [worker/README.md](../worker/README.md) for how a report gets there) into `inbox/`, so they
can be triaged without a Discord client open. The only things it ever writes back to Discord are a
reaction, to mark a report as pulled, and — once a report is marked resolved locally — archiving
that report's own thread.

Two Claude Code skills drive it day to day: **discord-reports-ingest** and
**discord-reports-resolve** (`.claude/skills/`). The scripts below work standalone too.

## Setup, once

### 1. A bot, not a webhook

The relay posts *to* Discord with a webhook; reading requires an actual bot user with a token.

1. <https://discord.com/developers/applications> → **New Application** → **Bot** → **Reset
   Token**, then on the same page enable the **Message Content** privileged intent. This never
   opens a gateway connection, only REST — but Discord strips content/attachments/embeds from REST
   responses for messages the bot didn't author unless that intent is on, so without it every
   report reads as empty.
2. **OAuth2 → URL Generator** → scope `bot` → permissions **View Channel, Read Message History,
   Add Reactions, Manage Threads**. Open the generated URL and add the bot to the server the forum
   lives in.
3. The channel is private (worker/README.md's step 1.2 denies `@everyone`) — server membership
   does not give the bot the channel. Open the channel's own **Permissions** tab and add the bot
   (or its role) with the same four permissions there.
4. Turn on **Developer Mode** (User Settings → Advanced), then right-click the server and the
   forum channel to copy the guild and channel IDs.

### 2. Local credentials

```sh
cd discord-reports
cp .env.example .env
$EDITOR .env    # DISCORD_BOT_TOKEN, DISCORD_GUILD_ID, DISCORD_CHANNEL_ID
```

`.env` is gitignored, same as `worker/.env` — it lives on your machine and nowhere else.

## Day to day

| | |
| --- | --- |
| Pull new reports | `./ingest.sh` |
| Close reports marked resolved | `./resolve.sh` |
| See what old closed copies would be purged | `./resolve.sh --cleanup` |
| Actually purge them | `./resolve.sh --cleanup --yes` |

`inbox/` is gitignored in full — it is pre-processed report content, the same category of thing
`report.md` itself is (arbitrary text a stranger typed).

## Layout

```
inbox/<thread_id>_<slug>/
  report.md       # exactly what the app sent — item, parse dump, comment, version meta
  screenshot.png  # the masked panel capture, if the reporter included one
  meta.json       # thread_id, url, title, tags, created_at, ingested_at, status, resolution, closed_at
```

Whoever investigates a report is free to add more files alongside these (a `findings.md`, say) —
nothing here reads or expects any particular shape beyond `meta.json`.

## How it tracks state

No local "last seen" file. "Already pulled" is a 📥 reaction the bot leaves on a thread's starter
message — open Discord and you can see at a glance which reports a run already took, and deleting
`inbox/` and re-running `ingest.sh` is always safe: already-reacted threads are skipped, everything
else is pulled fresh. "Resolved" is `meta.json`'s own `status` field, set by whatever investigated
the report — `resolve.sh` only ever reads that; it never decides it.
