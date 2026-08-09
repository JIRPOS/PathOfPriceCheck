# The updater, and how the application gets onto the disk

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

Two things, one subject: **how a copy of this program arrives, and how it replaces itself.** The
second cannot be designed without the first, which is why the Windows installer lives here rather
than beside the build instructions.

`src/update/` splits the way `src/data/` does. `release.cpp` and `install.cpp` are in `ppc_core`
and are pure — parsing, comparison, filesystem — so the whole of the decision-making is tested
headless in `tests/update_test.cpp`. `updater.cpp` is in the app target because it is the only
part that needs a thread, libcurl and SDL.

## The rule everything else follows

**Nothing is ever applied to a running program, and nothing closes on its own.** This is an
overlay over a game that people play for hours; an application that decides for itself to restart
is one that closes over a map. So the download is quiet, the notice is passive, and the swap
happens either when the user presses **Restart now** or as the application is closing anyway.

**Checked on the hotkey, not on a timer.** The check at startup is not the only one: `App::refresh_checks()`
starts another whenever an action gets past the foreground gate and the last one is over half an hour
old, so a session that runs for a day is not stuck on the release that existed when it started. It is
skipped while `has_news()` — a check that can only find the version already staged, at the cost of
taking the notice down while it runs. See [architecture.md](architecture.md).

**Applied on the way out, not on the way in.** `apply_on_exit()` runs after the worker is joined.
Doing it at startup instead would put the new file on disk while this process runs the old image,
so the update would need a *second* restart to take effect — which is not what the promise says.

## The four flavours

`detect_flavour()` answers how this copy got here, and `method_for()` turns that into what may be
done to it.

| Flavour | Detected by | Applied by |
| --- | --- | --- |
| `WinInstalled` | `HKCU\Software\PathOfPriceCheck\InstallDir` equals our own directory | running the next installer silently |
| `WinPortable` | Windows, and it does not | swapping the `.exe` |
| `AppImage` | `$APPIMAGE` is set | swapping, **at `$APPIMAGE`'s own path** |
| `LinuxBinary` | anything else on Linux, outside a `build/` directory | swapping the binary |
| `Unknown` | a build tree, a distribution package | **nothing** — a package manager owns those files |

Three ways to have news that cannot be acted on, and they are one answer to the user (the release
page): an `Unknown` flavour, a release with no asset for this platform, and an install directory
that is not writable — a `.zip` unpacked into `Program Files`. One answer, but **three notices**:
`Status::reason` records which it was, because a single sentence about permissions sent people
inspecting a directory that was writable all along when the real reason was a build tree. `install_dir_writable()` probes by
creating a file rather than by reading permission bits, because the bits are not the whole answer
on either platform; a 64-bit process gets a clean refusal, with no UAC file virtualization to be
fooled by.

It probes the directory of **the target**, never of `exe_path()`. Inside a mounted AppImage those
are two different files: the running binary lives on the read-only squashfs the runtime mounted
under `/tmp`, so probing beside it answers *no* for every AppImage there has ever been, and the
release page was offered to users whose install was perfectly writable. The target is the same
path the swap writes to, which is the only directory whose writability is the question being asked.

## The swap

Windows cannot overwrite a running image but **can rename it**, so `apply_swap()` renames the
target to `<name>.old` and renames the staged file in; `sweep_old()` clears the leftover at the
next start. POSIX renames straight over the target, which works against a running binary because
the inode is unlinked rather than written.

Either way **the last step is a rename**, so a machine that loses power mid-update still has one
whole executable at the path rather than half of one. The executable bit is set *before* the
rename, never after, for the same reason. When staging and install turn out to be on different
filesystems the POSIX path copies to a sibling of the target first, so the step that replaces the
target is still a rename within one directory.

`.old` is **appended**, not substituted for the extension: an asset name carries the version, so
the last dot in `PathOfPriceCheck-0.3.42-linux-x64` is not an extension at all.

The AppImage is overwritten **at `$APPIMAGE`**, not at `/proc/self/exe` — inside a mounted
AppImage the latter points into a temporary mount that vanishes on exit, and desktop integration
keys on the `.AppImage` path, so a new filename is a duplicate launcher entry.

## The Windows installer

`packaging/PathOfPriceCheck.iss`, Inno Setup 6, built by the release workflow. Per-user
(`PrivilegesRequired=lowest`) into `%LOCALAPPDATA%\Programs\PathOfPriceCheck`: no UAC prompt, and
a location that is writable by definition, which is what keeps the updater working for everyone
who took the recommended download.

**No vendor segment in any path this project chooses** — not above the install directory, not in
the start menu (`DisableProgramGroupPage`, one shortcut straight in the programs list), not in the
registry. `PathOfPriceCheck` is the only name that appears.

The updater runs it as `/VERYSILENT /NORESTART`, plus `/LAUNCH=1` only when the user pressed
**Restart now**. That switch carries a value it does not otherwise need because Inno's only
documented route to the command line from `[Code]` is the `{param:}` constant, which sees
name=value pairs and nothing else — there is no built-in for testing a bare switch. Handing it over **renames `staged` to `installer.exe` first**, and that rename is
what marks the update consumed: a swap consumes the file by definition, but an installer runs
*from* it, so without the rename every later exit would install it again. The leftover is deleted
at the next `init()`, by which point nothing is running from it. `CloseApplications=yes` lets Restart Manager close the copy being replaced, and
the `[Run]` entry guarded by `RelaunchRequested` is what brings the application back — which is
why an update applied at *exit* deliberately omits `/LAUNCH`: putting the app back up after the
user closed it would be the app deciding to run.

**Unsigned.** SmartScreen warns on first run. Said in the README and on the site rather than left
to be discovered.

## The release manifest

`latest.json`, a release asset, fetched from the fixed
`releases/latest/download/latest.json` URL — **deliberately not `api.github.com`**, for the reason
`data/updater.hpp` gives about the 60-requests-an-hour cap a shared address can exhaust. GitHub's
`latest` pointer already skips prereleases.

```json
{ "schema_version": 1, "version": "0.3.42", "notes_url": "…",
  "assets": [ { "name": "…", "url": "https://…", "sha256": "…", "size": 0 } ] }
```

`pick_asset` matches on the name's suffix and **never picks the `.zip` or the `.tar.gz`**, though
those are what a person downloads: what gets applied has to be a file that can be renamed into
place, and nothing here reads an archive container. The release therefore publishes the bare
executable beside each archive.

Versions are compared as **three numbers**, and only ever *strictly newer* — unlike the data
bundle, which is rolled back by publishing an older version, a binary downgrade would fight the
release the user chose. A version that will not parse is never newer than what is running, which
is why `Version::parse` refuses a leading `v`, a fourth field, surrounding space and a negative
(`std::from_chars` on a signed int accepts `-`, so that one is rejected explicitly).

**Nothing before 0.3 publishes this file**, so everyone updates by hand exactly once; a check
against a release that lacks it 404s and fails silently, which is correct rather than broken.

## Failure, and what is said out loud

Nothing. Per **failure is silent**, a failed check sets `Failed` and shows the plain version
number — not a warning colour, because the answer is the same either way. The detail goes to the
debug log under `[update]`, which is what a "why did it not update" report is read from.

## Driving it without a release

`PPC_DEV_UPDATE_URL=<url>` overrides where `latest.json` is fetched from. Until a release
publishes one there is no way to reach any state past "failed", and the three notice surfaces
cannot be looked at. The asset URLs inside it must still be `https` and their digests must still
match — only the manifest's own location is relaxed.

## What is not built

- **No delta or patch updates.** The whole executable is fetched each time. It is ~30 MB and
  releases are not frequent.
- **No rollback.** The `.old` file is a leftover to be swept, not a saved copy: it is deleted at
  the next start rather than kept, and going back is a download from the releases page.
- **No signature check beyond the digest.** The digest comes from the same host as the file, so it
  is an integrity check against a truncated or corrupted download, not an authenticity one against
  GitHub itself. Signing the release is the thing that would change that, and there is no key.
