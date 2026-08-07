# PathOfPriceCheck

A native, lightweight **Path of Exile price-check overlay** — C++20 and Dear ImGui on SDL3. No
Electron, no bundled browser, no wrapper runtime.

Hover an item in game, press the hotkey. The tool copies it, parses it, prices it against the
official trade API, poe.ninja and GGG's own in-game currency exchange, and draws the result over
the game in the game's own typeface.

> **Early development.** It works and it is used daily, but interfaces, settings and the panel
> layout all still move. Issues are very welcome; pull requests are not being accepted yet — see
> [CONTRIBUTING.md](CONTRIBUTING.md).

## What it does

- **One hotkey.** Ctrl+D over an item: the game's copy shortcut is synthesized for you, so there is
  no separate Ctrl+C to remember.
- **Reads the item properly.** Rarity, implicits, explicits, fractured, crafted, enchants,
  corruption, influences, quality — and the numbers the game leaves implicit, like quality-20
  DPS and where a base's defences fall in their own roll range.
- **Searches for what actually matters, and it is different for every kind of item.** A rare is
  searched on its modifiers, bounded by the tier each one rolled. A unique is searched on the rolls
  that *vary* — including the ones that vary without printing a range, which needs a per-unique
  modifier dataset to know about. A white, magic or rare item can be priced as its base type with
  item level and influences. A **map** is searched on none of its affixes, because a Chaos Orb
  re-rolls them all: it goes on tier, quantity, pack size, the drop bonuses a chisel added and
  whether dying in it voids the character. A **gem** goes on its name, level and quality and no
  modifier at all.
- **A reference price from poe.ninja** for the things a stat query cannot answer: uniques, gems,
  currency, and base types. Variants are resolved from the modifiers the copy in your hand actually
  rolled, and where two candidates cannot be told apart the row says so instead of guessing.
- **The in-game currency exchange, from GGG's own hourly digests.** A stack of currency, a scarab,
  an essence or a divination card is not sold through listings at all, so the trade site has
  nothing to say about any of them — the search below the panel would not be empty, it would be
  the wrong question. For those the panel shows what the market actually cleared at: the
  volume-weighted average, the band around it, and the volume on both sides. Items that trade
  there get no Search button, and one that had no trade in the last hour says so rather than
  showing you nothing.
- **Listings you can read.** Account, listing age and price, with the seller's own item drawn
  beside the list when you hover a row — through the same renderer as the item in your hand, so the
  comparison is like-for-like.
- **Polite to the APIs.** GGG publishes its rate limits in every response header; those are parsed,
  tracked and waited on proactively, and the state survives a restart. Searching is a button, not
  something that fires on every hotkey press.

## Requirements

| | |
|---|---|
| **Windows** | 10 or later. Single `.exe`, no runtime, no DLLs. |
| **Linux** | An **X11 session**. Wayland is not supported — it blocks global hotkeys and click-through overlays without portals this project does not yet use. |
| **The game** | Native client, or Wine/Proton. |
| **Game language** | **English only.** The whole tool works by matching the wordings the client prints, so a client set to any other language is not read at all. See below. |

### Only an English client, for now

Everything here starts from the text the game writes to your clipboard, and every modifier is
matched against the wording the client printed. Those wordings are language-specific, so a
non-English client produces item text this tool cannot parse — it will not mis-price an item, it
simply will not recognise one.

This is a **stretch goal, not a design limit.** The seam is already there and unused: every data
file is language-prefixed (`en-items.ndjson`, `en-stats.ndjson`), the bundle loader takes the
language as a parameter, and the manifest declares which languages a release carries. What is
missing is the work behind it — GGG ships localised stat-description files that the data builder
does not currently fetch, so no other language is built or published, and there is no setting to
pick one. Until that is done, the honest answer is "English clients only".

## Install

Grab the latest [release](https://github.com/JIRPOS/PathOfPriceCheck/releases). There is no
installer — unpack it and run it.

| | |
|---|---|
| **Windows** | `.zip` — one `.exe`, no DLLs beside it |
| **Linux** | `.AppImage` — bundles libcurl and OpenSSL, so it does not care which ones your distribution ships |
| **Linux** | `.tar.gz` — the bare binary, for anyone who would rather use their own libraries |

Both Linux builds are compiled against glibc 2.35 (Ubuntu 22.04, Debian 12) and neither can bundle
glibc itself, so an older distribution than that needs a build from source. If the AppImage refuses
to start with a FUSE error, your distribution no longer installs `libfuse2`: run it as
`./PathOfPriceCheck-*.AppImage --appimage-extract-and-run`, or install that package.

On first launch it downloads the game-data bundle (~4 MB) from
[PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data). Nothing is baked into the
binary, which is why a new league needs a data build rather than a new release of this.

Then open Settings (**Shift+Space** by default) and pick your league.

Building from source: **[BUILDING.md](BUILDING.md)**, which carries per-distribution package lists
for Arch, Debian/Ubuntu/Mint/Pop!\_OS and Fedora, plus the Windows toolchain.

## Using it

| | |
|---|---|
| **Ctrl+D** | price-check the item under the cursor |
| **Shift+Space** | Settings |
| **Escape**, click away, or the hotkey again | dismiss the panel |

Both hotkeys are rebindable in Settings, and both are ignored unless Path of Exile is the window in
front — they are grabbed system-wide, so they must not go off in your browser.

The panel docks beside the frame the item came from: right of the stash, or left of the inventory,
depending on which half of the screen your cursor was in. If it lands wrong, the stash and
inventory edges are sliders in Settings — set them against the live game rather than off a
screenshot.

## Documentation

| | |
|---|---|
| [BUILDING.md](BUILDING.md) | prerequisites per platform, build, test, run, sanitizers |
| [PRIVACY.md](PRIVACY.md) | every request it makes, every file it writes. Nothing is collected. |
| [ATTRIBUTION.md](ATTRIBUTION.md) | GGG, poe.ninja, poewiki, the libraries, the fonts |
| [EULA.md](EULA.md) | as-is, no liability, in plain language |
| [CONTRIBUTING.md](CONTRIBUTING.md) | what helps right now (issues), and why not PRs yet |
| [CONTACT.md](CONTACT.md) | one route: issues. Takedowns included. |
| [UNIQUE-MODS.md](UNIQUE-MODS.md) | the per-unique modifier dataset's contract, and its gaps |
| [CLAUDE.md](CLAUDE.md) | the architecture, and the reasoning behind every decision in it |

[CLAUDE.md](CLAUDE.md) is the real design document — it is written for an AI assistant working in
this repository, but it is the only place the hard parts are explained: why the clipboard is read
by hand instead of through SDL, what Wine does with the X selection and how it is coaxed out of it,
why quality is inverted the way it is, and which mistakes were already made and reverted.

## Privacy, in one paragraph

No account, no telemetry, no analytics, no update ping, and **no server on the other end** — this
project operates no backend at all. It talks to pathofexile.com, poe.ninja, GitHub and the PoE CDN,
identifies itself in the User-Agent as the API policy asks, and stores its cache and settings under
your usual config and cache directories. The clipboard is read only when you press the hotkey. The
debug log, which records clipboard contents, is off by default. Full detail in
[PRIVACY.md](PRIVACY.md).

## Versioning

`MAJOR.MINOR.BUILD` — `MAJOR.MINOR` lives in [VERSION](VERSION) and is bumped by the **Version
bump** workflow; `BUILD` is the cumulative CI run counter. Pushes to `master` publish a release for
win64 and linux.

## License

MIT — see [LICENSE](LICENSE) — **and as otherwise stated by the attributed projects**. The
libraries, the game data, the poewiki modifier groupings (CC BY-NC 3.0, non-commercial) and the
embedded Fontin typeface each keep their own terms; [ATTRIBUTION.md](ATTRIBUTION.md) lists every
one of them.

Path of Exile is © Grinding Gear Games. **This project is not affiliated with or endorsed by
Grinding Gear Games in any way.**
