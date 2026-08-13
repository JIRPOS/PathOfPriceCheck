---
name: discord-reports-resolve
description: Close out Discord threads for reports that have been marked resolved locally, and optionally purge old already-closed local report folders. Use when the user asks to close resolved reports, mark Discord reports as done, sync resolved status back to Discord, or clean up old report folders.
---

# Closing out resolved reports

The mirror image of **discord-reports-ingest**: that skill pulls reports in, this one pushes the
"done" state back to Discord and, only if asked, tidies the local copies of the oldest closed ones.

## What counts as resolved

Nothing here decides that. A report in `discord-reports/inbox/<id>_<slug>/` is resolved when its
`meta.json` has `"status": "resolved"` — written by hand, or by whatever investigated it
(**item-capture**, a person). Add a one-line `"resolution"` too if you know it (`"fixed in
a1b2c3d"`, `"wontfix: ..."`, `"duplicate of ..."`) — it isn't required, but it's the only place
that context survives once the folder is gone.

## Running it

```sh
cd discord-reports && ./resolve.sh
```

For every folder marked `resolved`, this archives and locks the matching Discord thread — the same
"handled" state [worker/README.md](../../../worker/README.md) describes for closing a report by
hand — then flips the local `status` to `"closed"` and stamps `closed_at`. The Discord thread is
never deleted, only archived: it stays the permanent, searchable record.

```sh
./resolve.sh --cleanup
```

Lists local folders `closed` for longer than `DISCORD_REPORTS_CLEANUP_DAYS`
(`discord-reports/.env`, default 90 days) **without deleting anything** — it's a dry run by
default. Show that list to the user before re-running with `--yes`:

```sh
./resolve.sh --cleanup --yes
```

This only ever removes the local pre-processed copy — the Discord thread it came from is untouched
and stays the record if anyone needs it later. Don't skip the confirmation step: deleting the local
folder is the one irreversible thing either skill does.
