# Conventions

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

- **Comments:** doc comments (Doxygen `///`) on public API; inline comments **only for the
  non-obvious** — hacks, surprising behavior, workarounds, protocol quirks. No narration of what the
  code plainly says.
- **Commit messages / PRs:** precise, not verbose — a summarised topic as the subject and
  `ADDED:`/`CHANGED:`/`REMOVED:` lines as the body, with a PR splitting those into a user-facing
  half and a reviewer-facing one. The whole spec, and why the release page depends on it, is the
  **commit-work** skill (`.claude/skills/commit-work/SKILL.md`) — it is written there rather than
  here because it is needed exactly when a commit is being written, and nowhere else.
- **The maintainer is `JIRPOS`.** Use the GitHub alias in every file — docs, licenses, anything
  published. The legal name goes in no file, here or in the data repo. Git's own `user.name` is a
  separate matter and is **not** to be changed: the commits are GPG-signed, the key is bound to
  that identity, and GitHub renders the commits under the alias anyway.
- **The repo is public.** The public-facing docs are `README.md`, `BUILDING.md`, `PRIVACY.md`,
  `ATTRIBUTION.md`, `EULA.md`, `CONTRIBUTING.md`, `CONTACT.md` and `LICENSE` (MIT), and the
  `User-Agent`'s contact URL points here rather than at the data repo. **`PRIVACY.md` enumerates
  every outbound request and every file written**, so a new host, a new cache file or anything new
  in the debug log is a change to that document as much as to the code — it is the one doc that
  goes stale silently. `CONTRIBUTING.md` says pull requests are not accepted yet.
