# Privacy

**This project collects nothing.** There is no account, no telemetry, no analytics, no crash
reporting, no update ping and no usage counter — and, more to the point, **there is no server on
the other end to collect anything with.** The project operates no backend of any kind. Everything
the application does happens on the machine it runs on, against third-party APIs that are the same
ones a browser would talk to.

No personally identifiable information is gathered, stored remotely, sold, or shared. The
maintainer cannot see that you ran this, what you priced, or that you exist.

## What the application talks to

Exhaustively — this is every outbound request the binary can make.

| host | when | what leaves your machine |
|---|---|---|
| `github.com` → `objects.githubusercontent.com` | at startup, and again only when a new data bundle exists | nothing but the request itself: the URL, and the `User-Agent` below |
| `www.pathofexile.com/api/trade/data/leagues` | when Settings is opened; cached 24 h | nothing but the request |
| `www.pathofexile.com/api/trade/data/static` | before the first search; cached a week | nothing but the request |
| `www.pathofexile.com/api/trade/search/<league>` | when you press **Search** (or on open, if you turned on `auto_search`) | the search query: trade stat identifiers and numeric bounds derived from the item under your cursor, plus the league and the listing-status filter |
| `www.pathofexile.com/api/trade/fetch/...` | after a search, and on **load more** | the result hashes the search returned |
| `web.poecdn.com` | when an item's symbol is first needed — a currency, or anything the in-game exchange trades; cached on disk forever after | nothing but the request |
| `web.poecdn.com/api/currency-exchange/<hour>` | when an item is priced and the newest published hour is not already on disk; **one download covers every item and every league** | nothing but the request |
| `poe.ninja/poe1/api/economy/...` | when a reference price is needed and the 30-minute cache has expired; **once per category**, not per price check | nothing but the request and the league name |
| `poe.ninja/favicons/favicon-32x32.png` | once, for the reference row's source mark | nothing but the request |

**Opening a search in your browser** builds the same query into a `pathofexile.com/trade/search`
URL and hands it to your browser. That costs no API call, and what happens after it is between you
and your browser.

Every request carries this `User-Agent`, and nothing else identifying:

```
PathOfPriceCheck/<version> (+https://github.com/JIRPOS/PathOfPriceCheck)
```

GGG's public-API policy asks unregistered clients to identify themselves and offer a route to the
maintainer, which is what that URL is for. It contains no machine id, no install id and no user
id — two identical installations send byte-identical headers.

As with any HTTP request, the hosts above see your IP address, and their own privacy policies
apply: [Grinding Gear Games](https://www.pathofexile.com/legal/privacy-policy),
[poe.ninja](https://poe.ninja), [GitHub](https://docs.github.com/site-policy). Requests go out
directly; nothing is proxied through anything of ours.

### No login, ever

The application never asks for account credentials, has no field to type them into, and uses only
publicly accessible endpoints. It cannot see your stash, your characters or your own listings.

It does keep a **cookie jar** at `<config>/cookies.txt`, because Cloudflare's `cf_clearance` and
whatever anonymous session cookie `pathofexile.com` hands out are what stop every launch from
re-running the edge's challenge. Those cookies are issued *to* an anonymous client, not derived
from any identity you provided. The file is created with restricted permissions — a session cookie
has no business being world-readable — and deleting it is safe at any time.

## Your clipboard

The whole tool works by reading the clipboard, so this is worth being precise about.

- The clipboard's **contents** are read at one moment only: after you press the price-check hotkey
  and the application has observed that something was actually copied. Nothing is read on a timer.
- Between those moments it watches an **ownership stamp**, not content — an opaque number the X
  server or Windows changes when someone writes the clipboard. It says *that* a copy happened, and
  reveals nothing about what.
- If you press the hotkey while something other than an item is on your clipboard, that text is
  what gets parsed. It fails to parse, nothing opens, and it is discarded. But it *was* read — so
  the ordinary caution applies: this is a global hotkey and the clipboard is a global thing.
- Nothing is ever written to your clipboard except when you click the diagnostic check id in the
  panel footer, which copies that four-character id and nothing else.

## What is stored on your machine

| path | what |
|---|---|
| `<config>/config.json` | your settings: league, hotkeys, panel geometry, listing status, result count |
| `<config>/cookies.txt` | the cookie jar above |
| `<cache>/data/<version>/` | the downloaded game-data bundle, plus a `current` pointer |
| `<cache>/leagues.json`, `<cache>/trade-static.json` | cached trade static data |
| `<cache>/ninja/` | cached poe.ninja overviews, pruned after a week unread |
| `<cache>/exchange/` | cached currency-exchange digests, newest two hours kept |
| `<cache>/icons/` | downloaded item and currency symbols, keyed by URL hash |
| `<cache>/trade-ratelimit.json` | the rate limiter's state, so a restart cannot walk out of a restriction |
| `<cache>/logs/` | the debug log, **only if you turned it on** — see below |

`<config>` is `$XDG_CONFIG_HOME/PathOfPriceCheck` (`~/.config/PathOfPriceCheck`) or `%APPDATA%\PathOfPriceCheck`.
`<cache>` is `$XDG_CACHE_HOME/PathOfPriceCheck` (`~/.cache/PathOfPriceCheck`) or `%LOCALAPPDATA%\PathOfPriceCheck`.
Deleting either directory is safe; the application rebuilds what it needs.

One optional setting is personal information you may type in yourself: **Account** in Settings
(`Name#1234`). It is stored in `config.json` and, as of today, is **not sent anywhere** — nothing
in the request path reads it.

Search results contain other players' account names and the whisper text for contacting them.
Those live in memory for as long as the panel is open and are dropped when the next check runs.

## The debug log

**Off by default, for everyone.** It exists because the clipboard handover between the game, Wine
and the X server fails in ways that are rare, unreproducible on demand and invisible afterwards.

When you enable it (Settings → Diagnostics), `<cache>/logs/ppc-<date>-<time>.log` records the copy
timeline, the trade query JSON, rate-limit decisions — and **the full contents of every clipboard
read, base64-encoded**, because whitespace and text encoding are exactly what those bugs turn on.
One file per run, the newest ten kept.

So: while it is on, item text you price is written to disk, along with anything else that was on
your clipboard when the hotkey fired. Turn it on to diagnose something, and **read a log before
attaching it to an issue.** Nothing sends it anywhere for you.

## Changes

This document describes the code in this repository at the commit you are reading it at. If a
release starts talking to something not listed above, that is a bug — please
[open an issue](https://github.com/JIRPOS/PathOfPriceCheck/issues).
