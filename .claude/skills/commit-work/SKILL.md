---
name: commit-work
description: Write a commit message, a pull request body, or release notes for PathOfPriceCheck. Use whenever the user says commit, commit and push, make a PR, open a pull request, or asks for a changeset — the project's ADDED/CHANGED/REMOVED shape is not the git default and the release page is generated from it.
---

# Committing and opening PRs

Precise, not verbose. The failure mode this shape exists to prevent is a wall of reasoning that
buries the two lines that mattered — the maintainer has pushed back on verbosity more than once, so
when in doubt, cut.

## The commit message

The **subject is the topic, summarised** — what this commit is about, not a list of what it
touched. The body is three labelled groups, always in this order and only the ones that apply:

```text
ADDED: <one line, one thing>
ADDED: <…>
CHANGED: <…>
REMOVED: <…>
```

One sentence per line, and for **nearly every line that is the whole entry**. Reasoning is the
exception, not the shape: add it only where the line reads as arbitrary or backwards without it —
a measurement that decided the design, a rule whose direction is not guessable. A reason under
every line is the failure mode, and it buries the two that matter.

Where a line does earn one, it goes on the following lines as bullet points, **with a blank line
before and after the bullets**. Without them GitHub folds the next `ADDED:` line into the bullet as
a lazy list continuation, and the body renders as nonsense.

Commits are GPG-signed under the maintainer's own `user.name` — **never change it**. Published
files use the alias `JIRPOS`.

## The pull request

A **pull request has two audiences and therefore two sections**, `## Release notes` first and
`## Review notes` second, both in the three groups above:

- **Release notes** is what a *user* gets out of the version: a new, changed or removed way to use
  the app, or behaviour they would notice. No identifiers, no filenames, no measurements, no
  bullets — a user does not care that a filter is called `mutated` or that 1896 listings were
  counted, only that a Foulborn unique is now priced apart from an ordinary one. A PR with nothing
  user-facing — a refactor, a CI fix, the version bump — **has no Release notes section at all**,
  which is the right answer rather than an omission.
- **Review notes** is the pooled commit lines: take each commit's own `ADDED:`/`CHANGED:`/
  `REMOVED:` lines, pool them, re-sort into the same three groups, and let the reasons ride along
  with the lines they belong to. Prefer the commit's wording over a fresh one — this half is a
  merge of what is already written, not a second telling of it.

**The release page is built out of the Release notes sections and nothing else.** The release job
asks the API for the notes `--generate-notes` would have written, opens every PR they name, and
keeps only that one section; the generated list of titles goes underneath as the index and the
attribution. So the same change is stated twice on purpose, once per audience, and the user-facing
half is written at the only moment anyone knows what it should say.

## Procedure

1. `git status` and `git diff` (plus `git log` for the branch's commits when writing a PR) — read
   what actually changed rather than what the conversation was about.
2. If the change touches a new outbound host, a new file written to disk, or anything new in the
   debug log, **`PRIVACY.md` is part of the change**. It enumerates all three and goes stale
   silently.
3. If the change contradicts a `docs/*.md`, update that doc in the same commit. `CLAUDE.md` itself
   only changes when the map does.
4. Branch first if on `master`. Commit; push and open a PR **only when the user asked for it** —
   pushing and PRing are not implied by "commit".
5. Do not wait on CI unless asked.
