# The in-game currency exchange (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/exchange/` is what a stack of currency, a scarab or a fragment is *actually* traded on. The
trade site has nothing to say about any of them — an exchange market is not a listing — so for
those items the search below the panel was never merely empty, it was the wrong question. This is
the right one, and it is GGG's own numbers rather than a third party's reading of them.

- **`https://web.poecdn.com/api/currency-exchange[/<hour>]`** is public and unauthenticated, needs
  no registered application, and is on the **CDN rather than the API host** — so it carries no
  `X-Rate-Limit` policy headers and must not go through `trade::request`, which exists to serve
  budgets this endpoint does not publish. What stands in for a limiter is that **one download
  covers every item in every league**: the cost is per *hour of play*, not per price check.
- **It is purely historical, in hourly digests.** The hour in progress does not exist — asking for
  it answers 404 with a well-formed `{"next_change_id":…,"markets":[]}` — so the freshest possible
  answer is the hour that just ended (`latest_hour`). The feed also publishes a few minutes late
  often enough to matter, so `load_digest` steps back up to `kStepBackHours`. An empty payload is
  never cached: it is "not published yet", not an answer, and `Digest::any_league` is what tells
  that apart from "this league does not trade" (plenty of markets, none ours).
- **A published hour never changes**, which is the whole cache design: the file name *is* the hour,
  there is no etag, no TTL and no freshness decision — the file on disk either is the hour being
  asked for or it is not. The newest `kKeepHours` (2) are kept at ~2MB each; the second exists so
  that stepping back an hour costs no second download.
- **The payload states every item by its `Metadata/Items/...` path and carries no names at all.**
  That is why `data::BaseType::metadata_id` exists — `BaseItemTypes.Id` in the game data *is* that
  path, and the data repo's `emit/items.py` now writes it through. **A bundle without the field
  simply has no exchange prices**, the same shape as `has_unique_mods()`: nothing here may assume
  one is there, and the app asks nothing when the item's base carries no id.
- **Ratios are ordered, not taken as named.** `lowest_ratio`/`highest_ratio` are integer counts of
  the two sides (`{A: 1, B: 50}` is one A for fifty B), so the price of one A in B is B/A — and
  "lowest ratio" is the lowest value of *item over against*, which is the item's **dearest** price.
  Reading the two names as a price band gets it backwards on some markets and right on others,
  because the counts move on both sides; `min`/`max` is what covers both. A market that saw no
  trade is published with zeros, and dividing by that would put a nonsense price on screen rather
  than none.
- **The volume-weighted average is the headline, and the band is the detail under it.**
  `volume_traded` is published for *both* sides of a market, and the two divided
  (`Rate::average`) are the mean ratio every trade in the hour cleared at — not the midpoint of
  a band whose ends one trade can set. Each market is drawn as a summary line and a small table
  beneath it: volume on both sides, then the lowest and the highest the hour saw. **Stock is
  deliberately not shown** — what is standing in the book says how long a sale would take, not
  what the item is worth. Zero volume on either side is no average at all (not a price of zero),
  and then the band itself is the summary.
- **A market is quoted against whichever of the two is worth more**, so one side is always a
  single unit: `4 x Winged Scarab = 1 x Chaos Orb` is how the trade is said in game and
  "0.25 chaos each" is not. The **average** is what decides the direction (`exchange::read`),
  because a band can straddle one — 0.5 – 2 chaos an ember — and then has no direction of its
  own; the top of the band stands in when there is no volume to average. It falls out that a
  Chaos Orb in hand reads `208 x Chaos Orb = 1 x Divine Orb` — the same rate the poe.ninja row
  states for both orbs, from GGG's own book and an hour old rather than half an hour.
- **Every item in the feed is drawn with its own glyph, not just the two denominators.** The
  feed keys on metadata paths and neither symbol source has ever heard of one, so the join is
  the **display name**: `TradeService::image_for_name` over every group of
  `/api/trade/data/static` that carries an image, falling back to
  `NinjaService::icon_for_name` across the overviews already held — the same two sources in the
  same order as every other symbol, and for the same reason (the trade static data is not
  fetched at all until the user's first search). The summary line draws the item's glyph
  **without** its name: it is the one row that has to fit both sides, "Cartography Scarab of
  Corruption" twice does not, and the full name is in every row under it.
- **The hour is stated in the user's own clock and date format** — the digest is addressed by a
  UTC hour, and a reader deciding whether a price is stale should have to do neither timezone
  nor date-order arithmetic. `strftime("%x %H:%M")` on `localtime`, so `App::run` sets
  `LC_TIME` from the environment (the opposite call to the `LC_NUMERIC` one beside it, and
  needed because a GUI-subsystem Windows binary has nothing else calling `setlocale`), and the
  line is drawn in `fonts.unicode` since a locale's date can be Cyrillic or CJK.
- **Only markets against Chaos or Divine are kept.** A scarab-for-essence market is a real market
  and no use to somebody asking what a scarab is worth.
- **A market only appears in an hour it traded in, and the hour cannot say why.** Measured on a
  live hour: 840 of the ~940 Allflame items that trade at all, and 115 of ~200 scarab varieties.
  So silence in the digest is two different facts wearing one face — *this does not trade here*
  and *nobody traded one this hour* — and for a thin item (a Weeping Essence of Greed) the second
  is the normal case. Reading it as the first left such an item with no exchange row, no trade
  search worth running and no poe.ninja price either: an empty panel, which reads as a check that
  gave up.
- **So which items trade there at all is a fact about the item, and it comes from the bundle.**
  `data::BaseType::exchange` is set by the data build, which crawls every hourly digest since
  Settlers launch and keeps the union of the metadata ids any market has ever named — 17.8k hours,
  ~1,000 ids, one boolean on a record that already carries `metadata_id`. The digest still
  supplies the *price*; this supplies the standing answer underneath it.
  **A bundle published before the flag must not read as "does not trade,"** which an absent
  boolean cannot say on its own — so the signal is bundle-level: `source.exchange_items` in the
  manifest, written through by `install` beside the attribution, and read back as
  `GameData::has_exchange_flags()`. Same distinction `has_unique_mods()` draws, drawn differently
  because that dataset is a whole file whose absence is the signal and this one is a field on
  records the bundle already had.
- **The exchange section has three states**, and the middle one is why the flag exists: a market
  this hour draws the table; no market but the item is flagged draws one line saying so; and not
  flagged, or a bundle that cannot say, draws nothing at all. Claiming either way from a bundle
  with no exchange data would be a guess.
  That middle line is **"no trades this hour" only when the hour was actually read** — a digest
  whose fetch failed says nothing about what traded in it, so that case says so instead. The
  distinction is invisible otherwise: a flagged item gets no trade search either way, so stating
  the market was quiet on the strength of our own failed request would leave the user no way to
  tell an idle market from a broken download.
- **An item that trades on the exchange gets no Search, no Open in browser and no filter list.**
  It has no listings for either button to find, and a button that can only ever come back empty
  reads as the item being unsellable rather than as the wrong market having been asked. The
  filters go with them (the panel layout is in [architecture.md](architecture.md)): they exist to shape a query nobody can run
  here, and their notes about unmatched modifiers charge the check with a failure it never
  attempted. One line says where the answer is instead.
  **That is keyed off the flag, not off the hour** — `App::trades_on_exchange()`. Keying it off a
  market in the last published hour gave a Weeping Essence of Greed a Search button on every hour
  nobody happened to trade one in, and the message alone would not have fixed that. It takes two
  sources, strongest evidence first: a market this hour *proves* the item trades there whatever
  the bundle says, which is also what keeps this working on a bundle older than the flag.
- **`ExchangeService`** is the fourth of the `LeagueService` family and the smallest, because the
  feed is: one digest in memory at a time, a lookup is a string compare, and the second check of
  the hour is free whatever the item. It is asked about **every** item, not only the ones planned
  as currency — whether a thing trades there is a fact about the market rather than about our
  strategy.
