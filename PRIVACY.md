# Privacy

**This project collects nothing.** There is no account, no telemetry, no analytics, no crash
reporting and no usage counter. Nothing is gathered in the background, on a timer, or as a side
effect of anything you do. Everything the application does happens on the machine it runs on,
against third-party APIs that are the same ones a browser would talk to.

**There is exactly one thing you can send us, and only by pressing a button that says so.**
**Report a bug** on the price-check panel opens a dialog that shows you the entire payload — the
item text, what the tool made of it, four version strings, whatever you type, and a screenshot
only if you tick the box — and sends it nowhere until you press Send. It goes to a small relay of
ours, described in full [below](#reporting-a-bug). That relay is the project's only backend, it
exists for that one button, and nothing else in the application ever talks to it.

The one thing that might sound like a phone-home is the update check, so it is worth being exact:
it downloads a small **static file** from the GitHub release page — the same bytes served to
everyone — and compares versions **on your machine**. It sends no version, no identifier and no
count; GitHub sees a file being fetched, exactly as it would if you clicked the releases page
yourself. It can be turned off in Settings.

No personally identifiable information is gathered, stored remotely, sold, or shared. The
maintainer cannot see that you ran this, what you priced, or that you exist.

## What the application talks to

Exhaustively — this is every outbound request the binary can make.

| host | when | what leaves your machine |
|---|---|---|
| `github.com` → `objects.githubusercontent.com` | at startup, and then at most once every 30 minutes, when you press one of the hotkeys; again only when a new data bundle exists | nothing but the request itself: the URL, and the `User-Agent` below |
| `github.com` → `objects.githubusercontent.com` | on the same occasions, for a newer version of the application itself, unless you turned **Update automatically** off; then once more to download it, only when there is one | nothing but the request. **This is a plain file fetch, not a version ping**: the same `latest.json` is served to everyone, the comparison happens on your machine, and nothing tells the other end which version you are on |
| `www.pathofexile.com/api/trade/data/leagues` | when Settings is opened; cached 24 h | nothing but the request |
| `www.pathofexile.com/api/trade/data/static` | before the first search; cached a week | nothing but the request |
| `www.pathofexile.com/api/trade/search/<league>` | when you press **Search** (or on open, if you turned on `auto_search`) | the search query: trade stat identifiers and numeric bounds derived from the item under your cursor, plus the league and the listing-status filter |
| `www.pathofexile.com/api/trade/fetch/...` | after a search, and on **load more** | the result hashes the search returned |
| `web.poecdn.com` | when an item's picture is first needed — a currency symbol, anything the in-game exchange trades, or the artwork of each unique offered for an unidentified one; cached on disk forever after | nothing but the request |
| `web.poecdn.com/api/currency-exchange/<hour>` | when an item is priced and the newest published hour is not already on disk; **one download covers every item and every league** | nothing but the request |
| `poe.ninja/poe1/api/economy/...` | when a reference price is needed and the 30-minute cache has expired; **once per category**, not per price check | nothing but the request and the league name |
| `poe.ninja/favicons/favicon-32x32.png` | once, for the reference row's source mark | nothing but the request |
| `ppc-reports.jirpos.workers.dev` | **only when you press Send in the bug reporter**, never otherwise | the report you were shown before you pressed it — see [below](#reporting-a-bug) |

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
- Item text you priced is held in memory until the next check, and is the one thing a **bug
  report** can carry off the machine — only the check you are looking at, and only if you press
  Send. See [Reporting a bug](#reporting-a-bug).
- **Two things write to your clipboard, both because you asked**: picking an entry from QuickPaste,
  which puts that entry's own text there and nothing else, and clicking the diagnostic check id in
  the panel footer, which copies that four-character id. On Linux the text is then served from a
  window this application owns for as long as it runs — which is how the X11 clipboard works for
  every program — so closing the tool takes it with it unless your desktop's clipboard manager has
  kept a copy.

## What is stored on your machine

| path | what |
|---|---|
| `<config>/config.json` | your settings: league, hotkeys, panel geometry, listing status, result count, filter ranges, client and interface language, panel opacity, whether to update automatically — **and your QuickPaste entries, in full**, since they are text you typed for this tool to hold |
| `<config>/cookies.txt` | the cookie jar above |
| `<cache>/data/<version>/` | the downloaded game-data bundle, plus a `current` pointer |
| `<cache>/update/` | a downloaded release of the application, waiting for the restart that applies it. One file, consumed as it is applied; absent whenever no update is pending |
| `<cache>/leagues.json`, `<cache>/trade-static.json` | cached trade static data |
| `<cache>/ninja/` | cached poe.ninja overviews, pruned after a week unread |
| `<cache>/exchange/` | cached currency-exchange digests, newest two hours kept |
| `<cache>/icons/` | downloaded item and currency symbols, keyed by URL hash |
| `<cache>/trade-ratelimit.json` | the rate limiter's state, so a restart cannot walk out of a restriction |
| `<cache>/PathOfPriceCheck.lock` | an empty-but-for-a-process-number file the running copy holds a lock on, so a second one refuses to start. Linux only; Windows uses a named mutex, which is not a file |
| `<cache>/logs/` | the debug log, **only if you turned it on** — see below |

`<config>` is `$XDG_CONFIG_HOME/PathOfPriceCheck` (`~/.config/PathOfPriceCheck`) or `%APPDATA%\PathOfPriceCheck`.
`<cache>` is `$XDG_CACHE_HOME/PathOfPriceCheck` (`~/.cache/PathOfPriceCheck`) or `%LOCALAPPDATA%\PathOfPriceCheck`.
Deleting either directory is safe; the application rebuilds what it needs.

**Two files outside those two directories, both beside the application's own executable and both
short-lived.** Applying an update replaces that executable, at its own path, and briefly leaves the
previous one beside it as `<name>.old` until the next start deletes it. And when a new version is
offered, an empty `.ppc-write-probe` is created and immediately deleted there, which is how the
application finds out whether it is allowed to update itself at all — asking the filesystem is the
only reliable way, since the permission bits do not answer it on either platform. Nothing else on
your system is written to. On Windows the installer —
if you used it rather than the portable `.zip` — additionally creates its own program directory,
its shortcuts, one registry value at `HKCU\Software\PathOfPriceCheck` recording where it
installed, and the usual Add/Remove Programs entry; uninstalling removes them.

One optional setting is personal information you may type in yourself: **Account** in Settings
(`Name#1234`). It is stored in `config.json` and is **never sent anywhere** — nothing in the
request path reads it, the bug reporter included. It is used for one thing: marking a listing in
the results as yours, and even a screenshot you choose to attach has it replaced along with every
other handle on the table.

Search results contain other players' account names and the whisper text for contacting them.
Those live in memory for as long as the panel is open and are dropped when the next check runs.

## Reporting a bug

The **Report a bug** button on the price-check panel. Nothing here happens unless you press it, and
then press **Send** in the dialog it opens.

**The dialog is the disclosure.** It shows the payload in full, in the same text that goes on the
wire, before anything is sent — there is no summary standing in for the real thing and no field it
does not display. Read it, and if you would rather not send some part of it, close the dialog.

What a report contains, exhaustively:

| | |
|---|---|
| the item | the clipboard text the game wrote, verbatim and unedited |
| the parse | what this tool made of that text: the fields it read, what they resolved to in the data bundle, which modifier matched which stat record and which matched none, and what a search would have asked for |
| your comment | the box you typed in, or nothing if you left it empty |
| four version strings | the application's version, the operating system's name (`Linux`, `Windows`), the league you have selected, and the data bundle's version. Nothing else, and none of them is per-machine |
| a screenshot | **only if you tick the box.** See below |

What a report does **not** contain: your account name, your character, any identifier of your
machine or install, any path from your disk, any cookie, and anything at all from a previous check.
There is no id tying two reports to one person, because there is no id.

### The screenshot

The checkbox starts unticked. The picture beside it is the exact image that would be attached, at
the moment you pressed the button, so the decision is one you can make by looking.

It is a **read-back of this application's own window**, not a capture of your screen: the pixels
this program drew, and only those. The game behind the transparent parts of the overlay is not in
it and cannot be — nothing here has the ability to photograph another window. Everything else on
your desktop is likewise absent.

**Nobody's account name is in it.** On an item that ran a search the panel shows a results table,
and before the picture is taken the panel is redrawn with every seller's handle replaced by its
position — `seller 1`, `seller 2` — so what is photographed never had a name on it. Yours is
covered by the same rule, on the row marked as yours. Prices, ages and everything else about the
market are left exactly as they were, because those are the thing a mispricing is read against.

What is in it, then, is the panel as you were looking at it with the names taken out — which is
still worth checking before you tick the box, and is why the preview is the size it is.

### Where it goes

To `ppc-reports.jirpos.workers.dev`, a Cloudflare Worker operated by the maintainer, which forwards
it to a private channel the maintainer reads and does nothing else with it. The Worker keeps no
database, writes no log of requests, and stores nothing: the report is relayed and the request is
over. Its source is in [`worker/`](worker/) in this repository, so what it does is readable rather
than promised.

Cloudflare sits in front of it and sees your IP address, as any host you make a request to does;
their [privacy policy](https://www.cloudflare.com/privacypolicy/) applies. The Worker uses that
address for one thing — an hourly cap, so the endpoint cannot be flooded — and it is never part of
what reaches the channel.

A report stays in that channel until it is dealt with. If you want one removed, quote its id: the
dialog shows it after a successful send and it is the only handle either of us has on it.

### Turning it off

There is nothing to turn off, because nothing runs. The button sends when you press it; if you
never press it, the application never contacts the relay and never has.

## The debug log

**Off by default, for everyone.** It exists because the clipboard handover between the game, Wine
and the X server fails in ways that are rare, unreproducible on demand and invisible afterwards.

When you enable it (Settings → Diagnostics), `<cache>/logs/ppc-<date>-<time>.log` records the copy
timeline, the trade query JSON, rate-limit decisions, the path this copy of the application runs
from — and **the full contents of every clipboard read, base64-encoded**, because whitespace and
text encoding are exactly what those bugs turn on.
One file per run, the newest ten kept.

So: while it is on, item text you price is written to disk, along with anything else that was on
your clipboard when the hotkey fired. Turn it on to diagnose something, and **read a log before
attaching it to an issue.** Nothing sends it anywhere for you.

## The website

Everything above is about the **application**. The project's website is a set of static pages
served by GitHub Pages, and it carries no analytics, no cookies, no trackers and no fonts or
scripts fetched from anywhere else — a visit is a request for the pages themselves and nothing
more. The landing page serves one script of its own, which drives the screenshot gallery's
arrows, its rotation and the pop-out an image opens in; it reads nothing about you, stores
nothing and requests nothing, and the gallery works without it. GitHub serves the pages and logs
the request as any web host does; that is between you and
[GitHub](https://docs.github.com/site-policy). The application never visits it, and downloads
go to the same `github.com` release assets listed above.

## Changes

This document describes the code in this repository at the commit you are reading it at. If a
release starts talking to something not listed above, that is a bug — please
[open an issue](https://github.com/JIRPOS/PathOfPriceCheck/issues).
