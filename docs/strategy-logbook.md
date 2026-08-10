# The logbook strategy

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

What every strategy shares — bounds, options, merged stats, the hidden section — is in
[item-layer.md](item-layer.md). This is the one item in the game that is **up to three items at
once**, and everything below follows from that.

An Expedition Logbook lists up to three **destinations**. Each names an area, the faction whose
land it is, and the two or three implicits that apply there. The player takes the book to Dannig
and travels to **exactly one** of them, and the faction is what decides what that is worth — so
one logbook has up to three prices, and a query asking for all three destinations at once prices
the single copy in the league that leads to that exact trio.

## Parsing (`item/parse`)

A destination is a section of its own and is recognised by **shape, not by a table of factions**
(`is_logbook_destination`). There are four factions today and the areas run to dozens; both are
game data a league can add to, and neither is a vocabulary this layer could keep honest. What is
matched instead is the two leading lines being **bare names** — no digit anywhere, no `Label:`
colon, no mod-type suffix, not an info line — which nothing else a logbook prints in a section of
its own is.

Two checks and both are load-bearing. The **leading pair** is what keeps the block of affixes a
rare logbook prints *below* its destinations out, because an affix opens with its roll. And the
**tail has to read as modifiers** (`looks_like_mods`), which keeps a stray pair of prose lines out
and is the half that still holds with Advanced Mod Descriptions on, where each implicit gains an
info line above it. The number of implicits is deliberately not fixed: the captures show two and
three.

The implicits stay in `Item::mods`, and `LogbookArea::mods` holds their indices. Which implicit
belongs to which destination is the whole of what a logbook is priced on and nothing about the
modifiers themselves says it — the rare capture grants "increased number of Explosives" from two
different destinations at two different rolls. The area and the faction go nowhere near the mod
list, where they used to arrive as six unmatchable lines.

**"Take this item to Dannig" needed a usage needle.** A logbook is gear, so prose needs a positive
signal before it stops being read as a modifier, and that sentence opens with no click
instruction — the same shape a chart's "Take this item to Valerie" already had a needle for.

## The join (`item/plan`, `logbook_stat`)

The faction and the area are searched as **`pseudo.*` stats**, and nothing else in the game is
searched this way: the item prints the two as bare names and the site words them as
`Has Logbook Faction: Druids of the Broken Circle` and `Has Logbook Area: Scrublands`. So the join
is by exact stat wording, through two lexicon terms (`Term::LogbookFactionPrefix`,
`LogbookAreaPrefix`) — the same shape and the same argument as `SanctumEffectPrefix`, which is
also why the four faction names are nowhere in this codebase. A name the bundle has no stat for is
a note, as an unknown sanctum boon or beast species is: the area list grows with the league.

## What is searched

- **One destination at a time.** `SearchPlan::choices` is one group per destination and
  `StatFilter::choice` files each row under one; `select_choice` keeps exactly one group live.
  The **faction is the choice itself** — `choice_primary`, drawn as the alternative's own row and
  never a second time as a tickable one, because it is the single filter that follows entirely
  from which destination was picked. Everything else in the group is offered unticked: where it
  goes, because a buyer picking a faction is rarely picking an area with it, and what it grants
  there, because an implicit is one of two or three numbers that came with the area rather than
  something anybody chose.
- **The faction is asked on presence, never on a count.** The pseudo stat does take a value — how
  many destinations belong to that faction — and it is not what decides the price: a logbook with
  two Druids destinations is still bought for a Druids run, and bounding it drops every
  single-destination copy of the same thing.
- **The first destination is live by default**, and it is the *first* on purpose. Nothing here can
  rank the four factions, the ranking changes with the league and with what the player is farming,
  and a default dressed up as an answer would be read as one. The panel puts them in the game's own
  order and the choice is one click.
- **A destination's implicit is a floor and never a ceiling** (`group_logbook_mods`). Trade indexes
  an item's implicits as one total per stat and all three destinations feed the same total — the
  rare capture's two Explosives rolls, 14% and 16%, are indexed as 30%. A floor still matches under
  that, since the total can only exceed one destination's own roll; a ceiling seeded from one
  destination's roll asks the other two not to exist. Which side is the floor is the stat's own
  `better`, the same question `to_filter` asks of an open bound.
- **The area level**, `map_filters.area_level`, a floor and ticked. Unlike a map's tier, a chart's
  area level or a sanctum's floor — all exact, because a different number there is a different
  product — a higher logbook area level is strictly more of the same one, and a buyer at 80 takes
  an 83.
- **The item level**, offered. It bounds what the affixes can be crafted to, which is a question
  about crafting the book rather than about running it.
- **Quantity, rarity and pack size**, offered, all three unticked. They are the same three
  properties a map is searched on and come off the same `map_filters` keys, but a map's are the
  whole of what it is run for and a logbook's are a second-order bonus on top of the artifacts,
  which the destination decides. **Not yet measured** for this category: a filter the site accepts
  and indexes nothing under empties the search, exactly as `heist_max_escape_routes` does, and
  unticked is the state that cannot do that. Measure one at a time before ticking any of them.
- **The type is sent only where the bundle resolved the base.** The category is the whole search on
  its own — one base type is filed under `logbook` — so the type says nothing it does not, and a
  magic logbook's printed line is "Buffered Expedition Logbook", which as a type matches nothing
  and reads as nobody selling one.

## What is not

**The affixes the book prints below its destinations** are the map argument and get the map's
answer, which is what the maintainer asked for: they apply wherever it goes, a logbook is
`craftable` and a currency redoes them, and a query naming them finds the one copy in the league
that rolled that set. So they are `hidden` — offered under the section at the foot of the list
rather than dropped on the floor — and, as a map's are, not notes either.

**Split is not parsed, not filtered and not mentioned.** It is a flag line like Corrupted and the
site has a filter for it; neither is wanted here. (The flag itself is read by `parse_flags` for
every item in the game and has been since long before this, which is a different thing from the
strategy having an opinion about it.)

## Measured

The rare capture, searched as the plan builds it — category `logbook`, type `Expedition Logbook`,
`map_filters.area_level` at min 80, `pseudo.pseudo_logbook_faction_druids` on presence, plus the
three ordinary booleans — returned **983 listings** in Allflame. So the category, the type beside
it, the area level and the faction pseudo are all indexed for this category and none of them is a
filter the site accepts and answers with nothing.

What has **not** been measured, one variable at a time and against this same capture, is
`map_iiq`, `map_packsize` and `map_iir` — see above.
