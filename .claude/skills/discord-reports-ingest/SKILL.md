---
name: discord-reports-ingest
description: "Pull new bug reports out of the private #ppc-reports Discord forum into a local, gitignored inbox for investigation. Use when the user asks to check Discord for new reports, pull in reports, or catch up on what's been reported — this only downloads and pre-processes, it never investigates or fixes anything itself."
---

# Ingesting reports from Discord

[worker/README.md](../../../worker/README.md) describes the relay: every bug report the app sends
lands as its own thread in the private `#ppc-reports` forum, with a `report.md` attachment written
to be pasted into a GitHub issue unedited. This skill is the other end — it reads that forum and
drops each report's content locally so a person or another agent can go through it without a
Discord client open.

**It does no investigation.** It downloads, tags with metadata, and stops. Diagnosing what a report
actually found is the **item-capture** skill's job, one report at a time, after this has run.

## One-time setup

[discord-reports/README.md](../../../discord-reports/README.md) — create the bot application,
grant it View Channel / Read Message History / Add Reactions on the forum channel specifically (it
is private, per worker/README.md's step 1.2), and fill in `discord-reports/.env` from
`.env.example`. Nobody has done this yet if `discord-reports/.env` does not exist — say so and
stop rather than guessing at credentials.

## Running it

```sh
cd discord-reports && ./ingest.sh
```

Each new report lands in `discord-reports/inbox/<thread_id>_<slug>/`:

| File | Contents |
| --- | --- |
| `report.md` | Exactly what the app sent — the item, the parse dump, the reporter's comment, version meta. Same file the doc says is ready to paste into an issue. |
| `screenshot.png` | The masked panel capture, if the reporter included one. |
| `meta.json` | `thread_id`, `url` (jumps straight to the Discord thread), `title`, `tags` (the forum's own triage tags), `created_at`, `ingested_at`, and `status` — starts `"new"`. |

"Already ingested" is tracked by reacting to the thread's starter message with 📥, not a local
state file — so it survives the inbox being deleted or moved, and re-running is always safe: a
thread the bot has already reacted to is skipped, everything else is pulled. If a report seems to
be missing after a run, check the bot actually has channel access before assuming it was already
handled.

## After this runs

Report the new folders to the user (count and titles is enough) and hand off — either they read
`report.md` themselves, or ask you to work through them with **item-capture**. Whoever investigates
a report writes their finding into that report's own directory (e.g. a `findings.md` alongside
`report.md`) and, once it's actually handled, sets `meta.json`'s `status` to `"resolved"` — that
field is what **discord-reports-resolve** watches for.
