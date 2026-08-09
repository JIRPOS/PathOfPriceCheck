# The poe.ninja reference price (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/ninja/` is the going rate a stat query cannot give. A Divine Orb, a level-21 gem and a
unique whose copies are all alike are worth what the market is paying, which is the one thing
poe.ninja measures and trade does not — and a **white, magic or rare item is priced as its base
type**, which is all poe.ninja knows how to say about one. It is one row under the filters
(`draw_reference_price`): the site's mark, the price, and the week behind it — and the whole row
is a click-through to the item's own page, which holds the variants and the history the row has
to leave out. A map is the only strategy with no row at all.

- **Only the economy endpoints are public API** (https://poe.ninja/docs/api). The builds and
  profile endpoints are explicitly closed to third parties; do not reach for them. The paths moved
  — `poe.ninja/api/data/currencyoverview` is a 404 now, and PoE 1 lives under
  `poe.ninja/poe1/api/economy/`. Two feeds with different payload shapes: `exchange/current/overview`
  is the currency market (id → chaos, plus `core.rates`), `stash/current/item/overview` is what
  individual items are listed at (name, base, variant, `chaosValue`, `sparkLine`).
- **It is not GGG traffic and does not go through `trade::request`.** That limiter serves GGG's
  published per-policy budgets; this is a different host with different rules, honoured differently:
  a **30-minute disk cache** (what poe.ninja sets on its own responses, and PoE 1 overviews only
  refresh every fifteen minutes anyway), a conditional request when that expires so an unchanged
  overview costs a 304, and one request per *category*, not per price check. The docs ask desktop
  apps to proxy through a backend; we have none, so the cache is what stands in for it.
  `ninja/cache` keeps the body verbatim after a one-line JSON header — a gem overview is four
  megabytes and escaping it into a JSON string field would cost more than the download did — and
  `prune` drops what nothing has read in a week, or the cache grows by every league ever played.
- **The divine rate comes from the Divine Orb's own line, not from `core.rates`.** That field is
  the reciprocal rounded to four figures, and converting the divine line's own price with it puts
  a Divine Orb at 0.9995 divine. Priced against itself it is exactly one. `quote()` switches to
  divine at one divine and rounds to what a price is actually said in; `quote_in` forces a
  currency, because both ends of a span have to be in one or "79.4 – 4" is what the pair reads as.
- **The Chaos and Divine Orb are answered with the rate, because their own price is a tautology.**
  The economy is denominated in them: poe.ninja quotes everything in chaos, so a Chaos Orb is one
  chaos, and `quote` converts anything past a divine, so a Divine Orb is one divine. Neither is
  what a player checks either orb for — the rate between the two is, it is one number (the divine
  line's chaos price), and it is the answer for both. `Reference::per` says the price is a *rate*
  and the row draws it as `201 x [chaos] Chaos Orb **per** [divine] Divine Orb`, which reads true
  whichever of the two is in hand. Everything about it comes from the divine line, including the
  sparkline — that trend is the rate's own — and only the click-through follows the item actually
  being checked. With no rate published (an SSF league) there is nothing to state, so both fall
  back to their own price. **Every other currency is priced exactly as poe.ninja reports it**;
  this is the one special case, and it is special because of what the numbers denominate.
- **A unique's variant is read off the modifiers the copy in hand rolled** (`narrow_by_mods`), and
  this is the difference between a right price and a ten-fold wrong one: Ralakesh's Impatience is
  three lines — Power, Endurance, Frenzy — 805, 133 and 75 chaos, and the item says outright which
  it is. poe.ninja's variant *labels* are its own shorthand and are never guessed at, but the
  modifiers behind them are the game's wordings. Two comparisons, because poe.ninja states a
  unique's rolled modifiers as ranges and its fixed ones as plain numbers: a range means only the
  wording is compared (`collapse_ranges` → `data::placeholder_form`, so `+(15-25)%` meets the
  game's `+22%`), while **a number with no range around it is fixed on that variant** — Mageblood's
  "Leftmost 4 Magic Utility Flasks" — and the line is compared verbatim. Mismatches are counted
  *relatively*, never absolutely: the two sources' wordings drift, and a miss every candidate
  shares says nothing. A candidate the source published with **no** modifiers is not scored at all
  and stops the whole comparison, or it would win on a mismatch count of zero — the opposite of
  what its silence means. Where that leaves several (Mageblood's flask count, Voices' passives) the
  row states the **span** and the count rather than picking one; a confident wrong price is the
  failure this whole layer avoids, and the click-through settles it.
- **A rolled item is priced as its base, on three things and only three**: the base, the **item
  level** and the **influences**. poe.ninja carries no quality on a base line, so a 20% quality
  base is priced as any other. The influence set is matched **exactly** — an uninfluenced
  Twilight Regalia at item level 84 is 5 chaos and a Warlord one is 1370, so falling back to the
  wrong influence would be wrong by two orders of magnitude — and it is compared as a *set*,
  because "Shaper/Crusader" is the site's ordering and an item is not priced differently for
  being read backwards. **Searing Exarch and Eater of Worlds are excluded**: they come from an
  implicit rather than from the base, poe.ninja does not split bases by them, and asking for
  them would match nothing (`examples/item_6` is exactly that item). Item level is bracketed —
  82 up, today — so the best bracket the item has reached is what it is worth, and the top one
  is an open end. Below the lowest there is no price, which is a fact about the item rather than
  a gap in the data (`examples/item_7`, item level 78). On a **rare** the note says outright
  that this is the bare base and its modifiers are the search below: without that, a floor price
  reads as the item's price. The base name is the only name matched — a rare's own name is
  randomly generated and a hit on it could only ever be a coincidence, and a wrong price.
- **Which overview to ask** is `keys_for`, off the trade category, and the currency market always
  leads: it carries the rate and the symbols every other price is drawn with. A currency item is
  looked for *in* it before anything else is downloaded — it holds the orbs and catalysts most
  currency checks are about — and only a name it does not have sends us to one of the seventeen
  other exchange overviews, chosen by a keyword on the item's own name. That keyword table exists
  because the clipboard cannot tell a Scarab from an Essence: both are the "Stackable Currency"
  item class.
- **A map item is a bulk good or an item, and the item level is what says which.** "Map Fragments"
  and "Misc Map Items" (`Item::is_map_fragment`) print `Rarity: Normal` because the game has no
  other rarity to print, and pricing one as a *base type* — which is what Normal used to mean here
  — asked poe.ninja a question about crafting bases and got nothing back, so scarabs, embers,
  splinters and invitations had no price at all. The split is the **item level**: one that prints
  none is identical to every other copy, has nothing to filter on and changes hands on the in-game
  currency exchange, so it is `Strategy::Currency`; one that prints an item level can carry a
  rarity and its own quantity/rarity modifiers exactly as a map does, is sold as an item, and falls
  through to the ordinary rule for its rarity. Both classes map to the one trade category
  `map.fragment`, so it says no more than "Stackable Currency" does and the **name** picks the
  overview: the keyword table first (Scarab, Allflame Ember, …), then `Invitation`, then
  `Fragment`. That routing (`ninja::map_item_type`) keys on the **category and name, never the
  strategy**, precisely because the item level moved some of these off `Currency` — asking the
  crafting-base overview about an invitation finds nothing. **`Invitation` is the one map item on
  the stash feed** — an invitation carries an item level, so poe.ninja lists it like an item rather
  than trading it in bulk (`/stash/current/item/overview?type=Invitation`, page slug
  `invitations`). Its item level is not part of the match: poe.ninja publishes one price per
  invitation.
- **A stack is priced as a stack as well as per item.** `Query::stack` comes off the
  "Stack Size: 6000/20" line and takes the **count, never the maximum** — that maximum is what one
  inventory slot holds, and a currency stash tab holds five or ten thousand in a single stack, so
  a count far past it is normal rather than a parse error. `Reference::stack_price` is quoted on
  its own rather than scaled from the unit price, because the two cross the divine line at
  different times: one chaos is one chaos and six thousand of them is 29.8 divine. Left at one for
  an `Ambiguous` price, where a span times a count would be four numbers. It is drawn on **a line of
  its own** under the unit price, which names its currency whether or not that is the unit's: the two
  do not fit the panel's width side by side, and `x6000 = 29.8 x` with the unit only on the row above
  is worse than either.
- **A gem is priced at the nearest tier poe.ninja publishes**, which is rarely the one in hand — it
  lists 1, 20, 20/20, 21/20 corrupted and nothing between. The best of those the gem has already
  reached is a floor on what it is worth, labelled with poe.ninja's own name for it so it is clear
  it is not this gem. Corruption filters first: it is a hard split, not a preference.
  It is looked up under **one** name and no other — `Item::gem_name()`, the same string the
  trade type is resolved from. poe.ninja spells a gem exactly as trade's `data/items` does,
  including "Vaal Blight (Blight of Atrophy)", so there is nothing to fall back *to*: it prices
  both "Blight" and "Vaal Blight", and a Vaal Blight — whose name line says "Blight" — reading
  the printed name would not miss a price, it would find a real line for a different gem at a
  tenth of the value.
- **A league poe.ninja has no economy for answers 200 with an empty payload**, not a 404 — so an
  SSF league parses fine and matches nothing, and that is reported as "tracks no economy for X"
  rather than as a broken response. The website's league slug is **not** the league id: "Hardcore
  Allflame" is `allflamehc`, not `hardcore-allflame` (`league_slug`).
- **`NinjaService`** is the third of the `LeagueService` triplets, with the work split the other
  way round: the worker only *downloads*, because matching an item to a priced line is pure and
  cheap, so it happens on the main thread and the second check of a kind is free. Overviews are
  kept for the life of the process and are about the economy rather than the item they were
  fetched for, so they are installed whatever the panel has moved on to. A check landing on a
  download already in flight cannot wait for it — that one was told to fetch a *different* item's
  categories — so `restart_` repeats it when that one lands.
- The row draws the currency symbol from the **trade** static data where that has been fetched and
  from poe.ninja's own payload where it has not: a reference price must not be the one thing that
  needs a trade request to render. The sparkline is drawn by hand rather than with `PlotLines`,
  which insists on a frame and a background.
- `item/plan` no longer notes that currency and gem pricing "is not implemented yet" — that note
  now sits directly above the price, and read as "this item has no price".
- **A `None` note never names poe.ninja**; it is drawn after the row's own "poe.ninja — " prefix
  and would read twice. The `Priced` and `Ambiguous` notes go in the tooltip instead, where
  naming it is right. The row's note wraps, because these say *why* there is no price and a
  sentence cut off at the panel edge does not.
