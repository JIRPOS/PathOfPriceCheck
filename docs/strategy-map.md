# The map strategy

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

One strategy covering maps, charts and Valdo maps. What every strategy shares — bounds,
options, merged stats, derived numbers — is in [item-layer.md](item-layer.md).

**`item/plan`'s map strategy** (`plan_map`, `add_map_pseudo`) is `Strategy::Map`, and it is the
one strategy that searches on **none** of an item's affixes. A map's prefixes and suffixes are
re-rollable with a single Chaos Orb; the buyer is choosing how dangerous a map they want, not
which affix it has, and a query naming them would find the one copy in the league that rolled
that set. So they are not filters and **not notes either** — they are left out on purpose, in
front of the reader (the item card beside the panel), and "unrecognised modifier: Players have
25% less Accuracy Rating" would charge the check with failing at something it never attempted.
What is searched instead:
- **Which area it is.** Every ordinary map now shares the one base type, printed as
  `Map (Tier 16)`, so the tier is the whole of what tells two apart; `parse_header` takes it off
  into `Item::map_tier` because no lookup knows the parenthetical, and `draw_name_plate` puts it
  back on screen. The filter is `map_filters.map_tier` with **min == max**: a tier-16 map is a
  different area from a tier-14 one, not a better one. A map that names its own area instead
  (`Shaper Guardian Map`, `Nightmare Map`) prints no tier and is matched by that name alone.
  A **unique** map is its name plus that tier; its own modifiers are on every copy. The
  `map` **discriminator** the bundle's `Map` record carries is load-bearing here rather than a
  tie-break: a query sending the type as a bare `"Map"` is accepted and matches nothing, which
  reads as an empty market rather than as a search that could not be built — so a bundle
  without that record gets a note instead.
- **What the map does for you.** `map_iiq` and `map_packsize` on by default, `map_iir` off —
  rarity is a preference, and imposing it drops the cheaper copies of the same map. All three
  come off the game's own property lines.
- **The four drop bonuses a Maven's chisel adds** — `More Maps`, `More Scarabs`, `More Currency`,
  `More Divination Cards` — which the game also prints as *properties* and which trade has no
  `map_filters` entry for. They are `pseudo.*` stats (`pseudo_map_more_map_drops` and its three
  siblings, which is all of them in `/api/trade/data/stats`), enabled when present.
- **How many affixes it has, as a total**, and **only on a corrupted map**: six is what every
  rare map has, and eight is what only corruption allows and most of what such a map is worth.
  `pseudo.pseudo_number_of_affix_mods`. The side of the pool is printed only by Advanced Mod
  Descriptions, so with that off there is no count to give and the plan says so rather than
  counting zero. Continuation lines are counted out — one affix can print two.
- **The implicit and any enchant**, on by default, and **on presence rather than on their
  numbers** — see the bound rule in [item-layer.md](item-layer.md), which a map is exempt from
  needing evidence for. The
  implicit is the one modifier a currency cannot re-roll, and it is what names the boss, the
  influence or the memory.
- **Blight, which is a filter and never a type.** The base line is the only place the clipboard
  says so — `Blighted Map (Tier 12)`, `Blight-ravaged Map (Tier 16)`, no flag line and no
  property — so `parse_header` sets `Item::blighted` / `blight_ravaged` off it and
  `resolve_base` then points the base at the ordinary `Map` every other one shares. Sending
  `"Blighted Map"` as the `type` is *accepted* by the site and matches nothing at all
  (measured: 0 listings against 1449 for the Map base plus `map_filters.map_blighted` at tier
  12), and no bundle carries a base under that name either. The two flags are mutually
  exclusive, so neither is ever asked for in the negative: a blighted map's own search already
  excludes the ravaged ones.

## Charts

**A chart is a map under another name and shares the strategy** (`plan_chart`), which is the
point rather than a shortcut: a Deepwater chart is an area with rolled danger and rolled
rewards, its prefixes and suffixes are the danger a buyer is choosing among rather than the
thing being bought, and trade puts it in the same filter group — `map_filters` is titled
"Map/Chart Filters" for that reason. So the affixes are left out exactly as a map's are, and
four things are added on top:
- **Which area it covers.** The game prints it as the leading prose line of the property block
  ("Seafloor Ridges"), i.e. `Item::type_line`, and trade takes it as the **type** — as an
  option carrying the `chart` discriminator, whose value is the area's *internal id*
  (`SeafloorRidges`). The bundle carries those records, but only under that id and with no
  display name anywhere on them, so `chart_area_key` turns the printed name back into one:
  apostrophes go, spaces and hyphens are word breaks, every word is capitalised
  ("Brine King's Domain" → `BrineKingsDomain`, "Clam-infested Shelf" → `ClamInfestedShelf`).
  **That convention is only ever a lookup key** — a record has to come back under it carrying
  the discriminator, or the search falls back to the chart's own base type ("Coral Reef Chart",
  which is a real search and simply a coarser one) plus a note. A wrong guess therefore costs
  breadth, never correctness.
- **The area's level**, `map_filters.area_level`, **exact** for the same reason a map's tier is:
  a level 83 area is a different area from a level 78 one, not a better one. Measured: 5526
  listings at exactly 83 against 10000+ for the area alone, so it does discriminate.
- **The shape** — `chart_shape`, whose five ids are `1`–`5` for End/Corner/Straight/Junction/
  Crossing. The game prints the option's own **text**, which is what makes the join possible;
  sending that text answers `{"code":2,"message":"Invalid chart shape"}` and fails the whole
  search, so it is a table in `plan_chart` copied from `/api/trade/data/filters`, the same
  closed-vocabulary argument as `status_options`.
- **The sulphur** ("Dead Man's Sulphur: +45%") as `chart_sulphur`, a floor and ticked: it is
  the league's own currency and therefore what the area is run for, so the same reasoning as a
  map's quantity rather than as its rarity.

Its **voyage modifier** needs nothing new — it is an implicit, so the map strategy already
enables it, **including on a chart nobody has sailed yet**, which prints only the promise of
one ("Voyage Modifier will be revealed once Charted"). That promise is a real stat with a real
trade id, not prose, and a buyer choosing between a revealed and an unrevealed chart is
choosing on exactly it.

## Valdo maps

**A Valdo map is the one map that is none of the above** (`plan_map`'s `reward` branch). It is
bought for the unique it pays out, its quantity and pack size come from unique modifiers rather
than from an affix roll, and so those are *offered* rather than imposed — they say nothing about
which Valdo map a buyer wants. What is searched is `map_filters.map_completion_reward`, an option
over the **unique list**: the game prints the payout as `Reward: Foil Hrimsorrow`, where the foil
is the reward's own variant, and sending that whole string answers
`{"code":2,"message":"Unknown reward output provided"}` — which fails the entire search rather
than widening it. So `find_unique_in` takes the longest run of those words that names a unique
the bundle knows, and a reward it cannot name becomes a note instead of a guess. The `Reward`
property is also the marker: no other map prints one.

The other half is **the only thing anything here is searched on in both directions**. Whether
dying in the map sends the character to the Void is what a buyer picks on, and a map that voids
is a different item from one that does not — so the copy in hand decides which question is asked:
present, and the search asks for it; absent, and it asks for the *absence*. Leaving it open
prices the two together. That is `StatFilter::negated`, which `build_query` sends as a second
stat group of type `not` beside the `and` one, carrying an id and no bounds. Measured on the same
reward: 133 listings that void against 101 that do not.

Two things this needed elsewhere. `StatFilter::mod_index` is an `optional` now, because a pseudo
total has no single modifier behind it; anything walking back to `Item::mods` has to check.
And **`SearchPlan::rarity` carries the trade `rarity` option**, because a unique map is planned
as a map — reading the option back off the strategy, as `build_query` used to, searched it
among the rares. It defaults to `nonunique`, since an empty one is a search across both markets
at once and nothing here ever means that.
