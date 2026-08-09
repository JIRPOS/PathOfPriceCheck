# The trade layer (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/trade/` turns a `SearchPlan` into a search on pathofexile.com and back into listings.

- **`trade/query`** — pure, no network: `build_query(plan)` is the search JSON, plus the URLs and
  the response parsers. **`StatFilter::inverted` is applied here and nowhere earlier**, and it flips
  the interval end for end as well as in sign: 77..90 as the game prints it is -90..-77 as the site
  indexes it, so a floor becomes a ceiling. Only ticked filters are sent. `group_for` is the
  contract with `item/plan`'s `NumericFilter::key` — the API nests every filter under a group
  (`misc_filters`, `armour_filters`, `weapon_filters`, `map_filters`) and rejects one filed in the
  wrong place. `option_group_for` is the same contract for `SearchPlan::options`, which go out as
  `{"option": …}` under `misc_filters`, `map_filters` or `ultimatum_filters`; an **unticked one is
  not sent at all** — whether an option has a row in the panel is the plan's business, and this
  layer only reads `enabled`. The ultimatum group is keyed off the `ultimatum_` prefix and carries
  all four of the things an Inscribed Ultimatum is bought for; its **area level is not one of
  them** and goes in `map_filters`, because the site files "Area Level" under Map/Chart filters
  whatever kind of item is asking.
  Whether the search names a `type` is the **plan's** call and this layer sends whatever it was
  given: a `Modifiers` plan leaves it empty (a rare is bought for its mods, and the category
  already says where those can live) **except on a flask**, where it names the base.
- **Which listings to ask for** is `Config::listing_status`, and it defaults to **Instant Buyout**
  (`securable`) rather than the API's older `online`. Not cosmetic: on one real capture the same
  query returned 4 matches In Person against 39 as Instant Buyout, because an offer that can be
  taken without the seller being at their keyboard is what most people now mean by "for sale".
  The five ids and their labels are GGG's own, copied from `status_filters` in
  `/api/trade/data/filters` — a closed vocabulary, so it is a table in `trade/trade.hpp` rather
  than something fetched, and unlike `league` a configured value **is** validated against it on
  load: an id GGG does not know makes every search fail with "Unknown status type".
- **`trade/ratelimit`** — the limits are not guessed at; GGG publishes them in the response headers
  of every request (`X-Rate-Limit-Rules` names the groups, `X-Rate-Limit-<group>` the
  `hits:period:restriction` rules, `-State` the server's own counters). So the first call under a
  policy is spaced against a seeded default and every one after it against measurement. **The
  state header outranks our own tally** rather than adding to it — it counts every client on the
  IP, including the user's browser tab. Time is a parameter, not a call, so the whole thing is
  unit-tested without sleeping.
- **`trade/ratelimit_store` — the limiter survives a restart.** `snapshot`/`restore` state the
  windows as *ages* and the restriction as *time remaining*, so they can be written under one
  clock and read under another; the store converts to absolute wall-clock ms in
  `<cache>/trade-ratelimit.json`, written on every request and every response. Without it,
  closing and reopening the app clears an active restriction it never actually served, and the
  seeded budget gets spent straight back into the lockout — repeatedly hammering through
  restrictions is how a client stops being throttled and starts being blocked. `restore` runs
  **before** `seed`, which then declines to overwrite it, and clamps what it reads (no negative
  window ages, no block over six hours) so a corrupt file cannot wedge the client shut. It is not
  a security boundary — deleting the file resets it — it stops the accidental circumvention,
  which is the one that happens. The decisions still run on `steady_clock`; the wall clock is
  only ever written down and read back.
- **How many listings to fetch** is `Config::result_count`, a Settings dropdown over 10/20/50/100,
  defaulting to **20**. This is a rate-limit choice and not a latency one: every ten listings is
  one more fetch request, and the binding policy (`50:300:300`) allows fifty fetches per five
  minutes before a five-minute lockout — so Top 20 is 25 price checks in that window, Top 50 is
  10, and Top 100 is 5 *and* trips the 16-per-12-seconds rule on two checks in a row. The cost
  also lands where the extra rows help least: only `min(want, total)` is fetched, so a rare with
  four matches costs one request at any setting, and the bill arrives on liquid items where the
  cheapest twenty already set the price. Settings states the cost on the row itself.
- **`trade/client`** — the one place outbound GGG traffic goes, `LeagueService` included. Waits out
  the limiter, issues the request, feeds the headers back in. A debt longer than 30s is returned as
  an error instead of waited on: a price check that lands four minutes late is about an item the
  user has sold. The wait is slept in slices against `cancel_waits()`, so shutdown does not sit out
  a restriction. `run_search` is the two-step flow — POST the query, then GET the first
  the requested number of hashes in batches of **at most ten**, which is a hard API limit. A batch that
  fails after an earlier one succeeded keeps what it has: ten listings are still a price, and the
  error explains the short list rather than replacing it. `fetch_page` is that batching loop on
  its own, which is also what **load more** spends: the search POST returns **100 hashes however
  large `total` is**, so paging deeper costs one /fetch and no search until those hundred run
  out — past which there is nothing to page to and the search has to be narrowed. `fetched`
  tracks hashes *asked for*, not listings received: a listing sold since the search comes back
  as a null element and is dropped, so paging off the listing count would re-fetch what was
  already seen.
- **`TradeService`** (`src/trade_service.cpp`) is `LeagueService`'s twin, for the same reason — every
  member on the main thread, the worker owning only its stack and the payload it hands over through
  the SDL event queue. The query JSON is built **on the main thread** and moved to the worker: the
  plan points into the data bundle the updater can swap at any moment.
- **Currency symbols** come from `/api/trade/data/static` (cached a week under `cache_dir()`), whose
  `image` paths are rooted at `web.poecdn.com`. `IconCache` (`src/icon_cache.cpp`) splits the work
  the way the threads force: the worker downloads (through a disk cache keyed by the URL's sha256,
  so a second launch makes no requests) and decodes to an `SDL_Surface`, and `pump()` uploads to a
  GL texture at the top of a frame, because only the frame loop has the context. `texture()` is
  allowed to answer "not yet" — a price with no symbol still prints its amount — and a URL that
  failed is never retried, or a 404 would be requested every frame forever.

The results are three columns — account, listing age, price — and the table takes **whatever is
left of the panel**, asked for explicitly rather than by bottom-aligning at height 0, because it
sits inside a child that can itself scroll and ImGui's bottom-align is not meaningful in one that
does. Sorting is `price: asc` and the site does it by chaos-equivalent, so a page of chaos prices
and divine ones interleave correctly and are **not** out of order.

The account is the **whole** handle, `Name#1234` — the digits are what tells two players sharing a
name apart — and is drawn in `fonts.unicode` (see [architecture.md](architecture.md)), since a Cyrillic or Korean handle is boxes in
Fontin. **A listing of the user's own is tinted green and says `(you)`**, matched against
`Config::account_name` when that has been filled in — case-insensitively, since the handle is typed
into Settings by hand and one entered with the wrong capital would fail to light up with nothing on
screen to say so. It is `ImGuiTableBgTarget_RowBg1`, which tints the alternating stripe rather than
replacing it, and the words are there because a green row is nothing at all to a reader who cannot
see green. Own listings are what a price is otherwise read straight off: one sitting at the top of
the page reads as the market's floor, which it is not.
The price copies the site's own form, `5 x [symbol] Divine Orb`: the symbol arrives off the
CDN in the background, so the currency is **named** as well as pictured and the row reads correctly
before it lands. A lowercase `x` rather than the site's `×`, which Fontin draws as `?`. The
listing's **gold fee** (`listing.fee`, a sibling of `price`, not a field of it) is shown, and
nothing at all when there is none — a tooltip that repeats the number under the cursor is noise,
which is also why the whisper text is no longer one: it is not something the user can act on from a
tooltip. The whole price cell is one hover target (`BeginGroup`/`EndGroup`), or the tip would appear
over the amount but not over the orb beside it.

**Only ever one tooltip per frame.** The fee rides *inside* the item popup below whenever that is
up, and `draw_price`'s own tooltip is suppressed — because `SetTooltip` and `BeginTooltip` build the
same window name, `##Tooltip_%02d` off the same `TooltipOverrideCount`, and only `SetTooltip` bumps
that counter (and only against a *previous frame's* still-active tooltip). Two of them in one frame
therefore `Begin` the same window twice, which appends rather than restarts **and drops
`SetNextWindowPos`/`SetNextWindowSize`**, since those are only honoured on a window's first `Begin`
of the frame. The symptom was the fee line printed inside the item card and the card itself
mis-sized and mis-placed. `AllowOverlap` on the row's `Selectable` is what lets both fire at once:
hovering the price cell hovers the row too.

**Hovering a row draws the seller's own item** over the panel, through the *same* renderer as the
item in hand — because the fetch response carries `item.extended.text`, base64 of the item in the
clipboard format PoE writes on Ctrl+C. So there is no second parser and no second view:
decode (`util/base64`) → `restore_mod_markers` → `parse_item` → `resolve` → `derive` →
`draw_item_tooltip`. **It is not byte-identical to a real copy: the site's renderer leaves the
mod-type markers off.** It writes " (enchant)" but never " (implicit)", " (crafted)" or
" (fractured)" — a fractured mod is simply printed first in the explicit block, with nothing in
the text saying so, and the parser typed every one of them Explicit (a fractured mod in the
explicit blue, an implicit inside the explicit block). The payload does say: each entry of
`implicitMods`/`explicitMods`/… carries a `domain`, and a fractured or crafted mod is listed
**among the explicits**, so the mod's own domain outranks the array it came in. `parse_fetch`
appends the suffix the game would have printed, matching a description against the line verbatim —
a line the site already marked matches nothing and is left alone. Verified against real fetch
responses for four fractured items, a crafted one and two enchanted ones. Parsed lazily
on hover and cached in `App::listing_items_`, resolved against the same pinned `item_data_`
snapshot, and dropped whenever a trade result lands. The row is a `Selectable` with
`SpanAllColumns | AllowOverlap` so the row is the hover target and lights up to say so, while the
price cell's own fee tooltip still sits on top. It is drawn into the **gutter** beside the panel
(the layout is in [architecture.md](architecture.md)), aligned to the top of its own row but never above the item card the gutter opens with, and
clamped by the height it drew at last frame so a long item does not run off the bottom — one frame
stale, which settles immediately, and there is no way to know the height before drawing it. That
clamp is a `max(min())` rather than `std::clamp`: on a card tall enough to leave less room than the
listing needs, the low bound is above the high one, which `clamp` is not defined for. Both position
and width are set explicitly:
`SetNextWindowPos` overrides a tooltip's follow-the-mouse placement and `SetNextWindowSize`
overrides its auto-fit **per axis**, so `(w, 0)` fixes the width and leaves the height to the item.
The width has to be fixed either way — `draw_item_tooltip` centres every line on
`GetContentRegionAvail()`, which in an auto-sizing window is whatever the last frame happened to be.

Searching is **on a button, not automatic**. `Config::auto_search` exists and defaults off: a
price check the user meant only to read the item with should not spend a request against their
rate limit. **Open in browser** builds the same query and hands it to the site in `?q=`, so it costs
no API call and always matches the filters as they are ticked *now* — the id of a search already run
would open whatever was ticked when it ran. When there is nothing to search the button says **why**
rather than only that: a stack of currency is bought in bulk on the in-game currency exchange and
has nothing a stat query could ask for, so its poe.ninja row is the whole answer and a bare
"Nothing to search" reads as a failure. Where there is no search *at all* — `trade::searchable`
is false, or the exchange feed has a market for it — the buttons, the filters, the strategy picker
and the plan's notes all go together: every one of them is about a query nobody can run, and a note
saying a modifier could not be matched charges a price check with failing at something it never
attempted. What replaces them is the item itself (above) and one line saying where the answer is
instead — see [exchange.md](exchange.md).

Rendering lives in `screens/item_view.cpp` (the game's palette: rarity-coloured name plate, grey
property labels, blue mods, light blue crafted/enchant, tan fractured, magenta scourge, red
corruption, per-element damage colours) and the filter list in `screens/pricecheck_screen.cpp`.
The panel is competing with the game's own tooltip for the same screen, so it prints **less** than
the clipboard does: `strip_roll_ranges` drops the range the game glues to a roll (`+86(77-90)` reads
as `+86`) in both the item text and the filter list, and everything the game prints *about* a
modifier rather than as part of it — what Advanced Mod Descriptions say (affix, tier, tags) and the
reminder text under a wording ("Unnerved enemies take 10% increased Spell Damage") — is a **hover
tooltip** on the modifier rather than lines around it. `draw_hover_tip` is that one place, and a
property uses it too: a utility flask's buff brings reminder text the game likewise keeps out of the
tooltip ("(Onslaught grants 20% increased Attack, Cast, and Movement Speed)"). Nothing is
lost: `Modifier::info_text()` carries the tier's range with it (`(Tier: 2 [77-90])`), a continuation
line repeats its affix because that is where the reader gets *its* range, and every derived number
is a small grey line under the property block it summarises — the DPS totals under the last damage
line, the base percentile under the last defence line.

**An unidentified unique is asked about rather than guessed at** (`draw_unique_choice`), above the
filters, because until it is answered they are filters on nothing. Its candidates come from the
bundle (`item/resolve`) and so does their **artwork**: `BaseType::art` is the path GGG's own CDN
serves the picture at, so `data::item_image_url` builds the URL and `IconCache` fetches it —
**the same picture the game draws, with nothing between the two**. Not poe.ninja: its overviews
carry a `web.poecdn.com` URL too, but only for what is being *sold* this league, which was 1193 of
the bundle's 1526 uniques and 27 of the Cobalt Jewel's 54. Off the bundle it is 1416 and 53.
The **base's inventory footprint** (`BaseType::w`/`h`) is both the size asked of the CDN and the
aspect it is drawn at — squashing a 2×3 body armour into a square is what makes two candidates
hard to tell apart at 46 pixels — and it comes off the base because a unique is not a base type in
the game's data and carries no size of its own. **The art is still never load-bearing**: 110
uniques have no path, an older bundle has none at all, and a download can be in flight, so a
candidate with no picture puts its name in the space the picture would have taken.
Two shapes, and the list's own height picks between them: **one per row with the name** while that
fits half of what is left of the panel, and past it a **grid of artwork alone** with the name on
hover, because fifty rows would push the prices and the item itself off the panel entirely. Each is
a `Selectable` with the picture drawn on top of it — the poe.ninja row's shape, and `AllowOverlap`
is what lets the two overlap — and the strategy picker grows a **change** button beside the name so
a choice, including the one the app took for itself, can be taken back.

**The filter list is a four-column table** (`draw_filters`), so that every row's numbers sit under
the previous row's: the toggle, the wording, where the modifier came from, and what the search asks
for. The **wording is second, straight after the tick**, because it is the only column every row
has something to put in — a pseudo total has no affix behind it and a roll on an item with Advanced
Mod Descriptions off has no code, and a gap between the tick and the text reads as a missing
checkbox. It takes the stretch column; everything else fits its content.

Column three glues the code to the modifier's own range — `P2[77-90]` is a tier-2 prefix, `S1` a
suffix, `R` crafted, `Impl` an implicit, `Frac` a fractured affix — with one code per modifier
`merge_same_stat` folded in (`StatFilter::merged`), joined as `P3+P1` and then dropping the range to
a line below, since it is the pair's total and belongs to neither code alone. An eldritch implicit's
rank (`Modifier::qualifier`) goes on its own line for the same reason a compound's range does: the
column is as wide as its widest row, and `Impl Lesser` on one line sets that width for every
modifier in the list. **The colour is the side of the pool and the letters are what put the modifier
there** — red prefix, blue suffix, as the trade site does it — so the two never compete for the same
four characters: a fractured prefix is a red `Frac`, and what a buyer needs to know about it first
is that it is fractured.

Column four is **what the search asks for**, and it is last rather than beside the code because it
is the one thing here that becomes editable: `46-48` between two bounds, `≥46` for a floor, `≤50`
for a ceiling (**borrowed** glyphs — Fontin's own are blank outlines, see Fonts above — spelled out
as `>=` and `<=` where there was nothing to borrow from), **nothing at all** for a
filter that only asks the modifier to be present, and **`absent`** for one asking that it not be
there (`StatFilter::negated` — a Valdo map that does not void). That last one belongs in this
column and nowhere else: the row is otherwise identical to one asking *for* the modifier, and a
tick beside a wording the item does not have reads backwards. A `misc_filters` boolean puts
**`yes`/`no`** there, and only the flags the plan marks `shown` get a row at all — see
`item/plan`. It is `StatFilter::min`/`max`, while the origin
column is `roll_min`/`roll_max` — what the modifier *can* roll against what the search asks of it,
which is why they are two fields: the range-match setting is exactly the distance between them, so
`[77-90]` beside `81-90` is the 5% window doing its job. The numeric filters share the table, so a defence
and a modifier line their numbers up in the same column. Row pitch is squashed (`CellPadding`,
`ItemSpacing`) and the checkbox is drawn at zero `FramePadding`, i.e. a square the height of a line
of text: at the default it is taller than the wording beside it and sets the pitch for the whole
list. Rows are told apart by **alternating background** (`ImGuiTableFlags_RowBg`) and not by
separators: a modifier can wrap onto three lines and its origin onto two, so what the reader needs
is to see where one row ends — and a rule between every pair would spend a line of height per
filter to say it.

**Why a row is not ticked is a tooltip on the wording** (`StatFilter::caveat`), never a line
under the list. The panel is competing with the game for the same screen, a note repeats a
wording that is one row above it, and the row already carries the whole statement: this modifier,
not searched. What is left under the list is only what has **no row at all** — a modifier nothing
matched, a wording two stats share, a pool the data states but never enumerates — because for
those there is nowhere else for the app to say it is leaving something out.

The item and its plan live on `App`, alongside **the bundle snapshot they were resolved against** —
`item_data_` is held separately from `data_` because the updater swaps that from its own thread and
the item holds raw pointers into it.

`PPC_DEV_ITEM=<file>` (with `PPC_DEV_OVERLAY=1`) opens the price-check panel on a captured clipboard,
which is the only way to iterate on it without the game running. Captures live in
`tests/data/examples/` — each `item_N.txt` is a real clipboard capture paired with `item_N.jpeg`, a
screenshot of the same tooltip, which is what the rendering is checked against. `tests/data/items/`
holds the captures with no screenshot beside them — two transcribed from one (the rapier and the
Elder bow), the map fragments and invitation, the eleven `map-*.txt` maps (every shape one comes in:
tiered rare, corrupted eight-mod, chiselled for extra drops, magic, white, unique, the two that
name their own area instead of printing a tier, a blighted one — whose base line is the only
statement of that — and a Valdo one, which is searched on none of the things any of the others
are), the five `gem-*.txt` gems (a Vaal gem, whose second half is the only thing naming it; a
transfigured one; two supports, one of them with a socket requirement far above its own level; and
one with quality), `card-blazing-fire.txt` and `currency-essence.txt` (the two shapes of thing the
in-game exchange trades in bulk that are neither an orb nor a fragment), and
`currency-chaos-stack.txt`, which is **written rather than captured**: it is a 6000/20 stack, the
case a currency stash tab makes ordinary and nothing else covers. The two
`unique-unidentified-*.txt` are written for the same reason and want replacing with real
captures — they are the two answers a base gives, a Goathide Gloves that could be either of two
uniques and a Cobalt Jewel that could be any of fifty-four, which is what puts the picker into its
grid. The three `chart-*`/
`listing-chart-*` files are every shape a chart comes in — one with Advanced Mod Descriptions on,
one without and carrying the sulphur property, and one unidentified, which prints no name line at
all. Three more cover the
`misc_filters` properties: `currency-facetors-lens.txt` (the one currency item a search can tell
two copies of apart), `memory-strands-boots.txt`, and `listing-intangibility-ring.txt` — which is
captured from a **listing** rather than from the game, and is named so, because the site's
renderer writes the keyword-link markup `[Intangibility|Intangibility]: 8%` where the clipboard
writes a plain label. That `listing-` prefix is what every capture taken from a fetch response
carries, and it means one more thing: the site's renderer leaves the mod-type markers off, so
those files hold the suffix `restore_mod_markers` puts back — which is what the app hands
`parse_item` for a hovered listing, and without it a chart's implicit reads as an affix. `strip_link_markup` in `item/parse` is what drops it, and without that a
hovered listing drew the markup and its property matched nothing anything looks up by name.
Prefer a real capture for anything new.

**Pin numbers to those captures, not to another tool's output.** The Q20 DPS formula was chosen
because it reproduced a number read off a screenshot of a reference tool, which turned out to be
unverifiable — and it disagreed with the one real capture that could discriminate. Ask for a capture
of the concrete case instead; the maintainer can reproduce one in-game.

**Numbers must never go through the C locale.** The game writes `1.79`; `strtod` under a `cs_CZ`
`LC_NUMERIC` reads that as `1`, and every DPS number downstream was wrong. Parsing uses
`std::from_chars` (locale-independent by definition) and `App::run` forces `LC_NUMERIC=C` for
formatting — *after* the tray and window exist, since SDL's X11 backend (XIM) and the tray's GTK both
call `setlocale(LC_ALL, "")` during init and would undo an earlier attempt. **`LC_TIME` goes the
other way**, set from the environment in the same place: a date is written for the reader, not for
the game, so it is written the way their machine writes one. It has to be explicit rather than
inherited because nothing calls `setlocale` at all on Windows — a GUI-subsystem binary gets neither
XIM nor GTK — and the C locale's date is the invariant-looking format this avoids.
