# Attribution

The code in this repository is MIT licensed ([LICENSE](LICENSE)). It is built on, and would not
work without, the projects and data sources below, which keep their own terms.

## Grinding Gear Games

**Path of Exile is © Grinding Gear Games.** This project is a free, non-commercial fan tool, and
is **not affiliated with or endorsed by Grinding Gear Games in any way.**

Item names, modifier wordings, the clipboard format the game writes on Ctrl+C, and the stat
identifiers a trade query is built from are GGG's. None of it is baked into the binary — it is
downloaded at runtime from [PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data),
whose [DATA-LICENSE.md](https://github.com/JIRPOS/PathOfPriceCheck-Data/blob/main/DATA-LICENSE.md)
states what that bundle contains and how it was derived. No game assets — images, sounds, models —
are copied or redistributed anywhere in this project.

### How this client uses GGG's API

Only **publicly accessible** endpoints are used; this application is not registered with GGG, has
no API key, and never asks the user for account credentials.

- **It identifies itself.** Every request carries a descriptive `User-Agent` naming the tool, its
  version and a URL that leads to a way of reaching the maintainer:
  `PathOfPriceCheck/<version> (+https://github.com/JIRPOS/PathOfPriceCheck)`.
- **It obeys the published rate limits rather than guessing at them.** GGG returns the active
  policies in the response headers of every request (`X-Rate-Limit-Rules`, `X-Rate-Limit-<policy>`,
  `X-Rate-Limit-<policy>-State`, and `Retry-After` on a 429). Every GGG request goes through one
  shared limiter that parses those headers, tracks each window, and **delays proactively** instead
  of absorbing 429s. The server's own state counters outrank our tally, because they count every
  client on the address — the user's browser tab included.
- **The limiter survives a restart.** An active restriction is written to disk and restored on the
  next launch, so closing and reopening the application cannot be used — accidentally or otherwise
  — to walk out of a lockout it never served.
- **Requests are made only when the user asks for them.** A price check parses and displays the
  item without touching the network; the trade search is a button, and `auto_search` is off by
  default. Static data (leagues, currency symbols) is cached for 24 hours and a week respectively.
- **It fetches in the batch sizes the API specifies** — ten listing ids per `/fetch` request — and
  asks only for as many listings as the user configured.
- **The in-game currency exchange feed is treated as its own thing.**
  `web.poecdn.com/api/currency-exchange` is public and unauthenticated, and it is on the CDN rather
  than the API host — so it publishes no rate-limit policy, and it is deliberately *not* sent
  through the limiter above, which exists to serve budgets this endpoint does not state. What
  stands in for one is that a published hour never changes and one download covers every item in
  every league: the cost is per hour of play rather than per price check, and a digest already on
  disk is never re-fetched.

If anyone at GGG wants a change made here, [an issue](https://github.com/JIRPOS/PathOfPriceCheck/issues)
is the route and it will be actioned. See [CONTACT.md](CONTACT.md).

## poe.ninja

The reference price row — what a unique, a gem, a currency item or a base type is currently going
for — comes from [poe.ninja](https://poe.ninja), used under its
[public API documentation](https://poe.ninja/docs/api).

- **Only the economy endpoints are touched.** The builds and profile endpoints are explicitly
  closed to third parties and this application does not request them.
- **Responses are cached aggressively**, matching the 30 minutes poe.ninja sets on its own
  responses, revalidated with a conditional request so an unchanged overview costs a 304, and
  fetched **once per category** rather than once per price check.
- The site's own favicon is drawn as the row's source mark, and the row is a click-through to the
  item's page on poe.ninja, so the data is credited where it is shown and the reader can go to the
  source for the variants and history a single row has to leave out.

## Path of Exile Wiki

The data bundle's per-unique modifier dataset — which modifiers a given unique can roll, and which
of them come from a random pool — is not in the game files. That grouping comes from
[poewiki](https://www.poewiki.net)'s `item_mods` cargo table, licensed
**[CC BY-NC 3.0](https://creativecommons.org/licenses/by-nc/3.0/)**: attribution required,
non-commercial use only, which this project is.

The attribution string travels with the bundle rather than staying behind in the publisher's
repository — the manifest carries it, the installer writes it through, and Settings renders it —
because an attribution that does not reach the user is not an attribution.

## Tooling behind the data bundle

The data repository decodes GGG's own `.datc64` bundles with
[pathofexile-dat / poe-dat-viewer](https://github.com/SnosMe/poe-dat-viewer) and
[dat-schema](https://github.com/poe-tool-dev/dat-schema).

## Prior art

[Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade) (MIT) is the tool this one is in
the spirit of, and reading it is how several of the problems here were understood to be problems.
No code is taken from it.

## Libraries

Fetched from source at pinned tags by CMake; none are vendored into this repository.

| project | license | used for |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | zlib | window, GL context, events, tray icon |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | the entire UI |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | every JSON payload and the config file |
| [doctest](https://github.com/doctest/doctest) | MIT | tests |
| [libcurl](https://curl.se) | curl (MIT/X derivative) | HTTP; system package on Linux, static Schannel build on Windows |
| [zlib](https://github.com/madler/zlib) | zlib | gzip for the trade data endpoints, on the Windows curl build |

`stb_truetype` and `stb_rect_pack` (public domain / MIT) ride along inside Dear ImGui and rasterize
all text.

## Fonts

The UI renders in **Fontin** by Jos Buivenga ([exljbris](https://www.exljbris.com/fontin.html)),
the typeface the game itself uses. Four faces are embedded in the executable.

Fontin is free for personal and commercial use, but its license nominally forbids redistribution
without the author's permission, which bundling into a released binary arguably is. That is a
deliberate maintainer decision with its reasoning written down in
[assets/fonts/README.md](assets/fonts/README.md), along with how to swap the typeface out — it is
one generated file and no code change. `PPC_FONT_DIR` already overrides the embedded faces at
runtime. **If you are Jos Buivenga and would prefer this not be bundled, open an issue and it will
be removed.**

For text this project did not write — trade account and character names, which are routinely
Cyrillic, Hangul or CJK, none of which Fontin covers — a fallback face is loaded from **whatever
the operating system already ships**. Nothing is bundled for it and nothing is redistributed.

## Icon

`assets/popc_icon.png` and the `.ico` generated from it are part of this repository and covered by
its MIT license.
