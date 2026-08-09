# The item layer (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/item/` turns clipboard text into a structured item, resolves it against the bundle, and says
what a search for it would ask for. Four steps, deliberately separate: only the middle two need a
bundle, and only the third and fourth encode pricing judgement.

- **`item/parse`** — pure, no I/O, and no bundle beyond the `data::Lexicon` that says what
  language the client wrote in: `parse_item(text, lex) -> optional<Item>`. Every word quoted
  below is the English entry of that table rather than a literal in this file — see
  `data/lexicon`. Sections split on
  `--------`; the header gives `Item Class`, `Rarity` and one or two name lines. Section *kind* is
  decided in a fixed order (flags → `Requirements:` → `Sockets:` → `Note:` → cosmetic → all-property
  → the first mixed block → usage note → bottom prose → gem lines → mods), because PoE prints no
  other marker. Load-bearing bits:
  - A property line is `Label: value` **with no digit in the label** (mod wordings contain colons).
    The property block's first prose line is the item's type, later prose starting with a digit is a
    flask's own effect and anything else is a property the game writes as a sentence ("Lasts 7.20
    Seconds"). That mixed block is only recognised as **section 1**, so a flavour line that happens
    to hold a colon ("simple ethos: why make the effort") cannot become a property. A property line
    is also the evidence that section 1 *is* the property block — except on a **flask**, which
    always has one and prints no `Label: value` line in it **unless it has quality**: an unquality
    flask turned "Lasts 6 Seconds / Consumes 40 of 60 Charges on use / Onslaught" into modifiers,
    so the class carries the rule instead. The type line never holds a number for the same reason.
    A parenthetical in that block is the buff's reminder text ("(Onslaught grants 20% increased
    Attack, Cast, and Movement Speed)") and rides on the property, as a modifier's does.
  - **Prose is not a modifier, and there are three kinds of it.** A rare's own mods can read as
    prose ("Players cannot Regenerate Life"), so flavour text needs a positive signal: a quoted
    block, or the last prose block of a *unique* that already has mods. The usage note underneath
    it ("Right click to drink…", "Place into an item socket…") is recognised by wording, because it
    sits exactly where flavour does — a flask has both, and taking the last section as flavour turned
    Rumi's Concoction's verse into three unmatchable mods. A leading `-` only reads as a negative
    roll with a digit behind it, or the attribution line "-Rumi of the Vaal" is a mod. Everything an
    info line or a mod-type suffix touches is mods, whatever the prose heuristics say.
    Those rules exist to tell a rare's mods from its prose and fire on the *rarity* line, which is
    why **`Item::is_gear()` is false for a map fragment**: a scarab has no modifiers at all, so its
    effect and its verse both used to come back as unrecognised ones. Its first prose block is the
    description and anything after it is flavour — the Maven's Writ prints only a verse and there
    is nothing to tell the two apart, so that one is read as the description.
    It is false for an **itemised beast** on the same argument from the other direction: a beast
    prints "Rarity: Rare" because it rolled monster modifiers, and those come from the captured
    monster's own pool rather than from an affix pool on a base — nothing to tier, to bound or to
    match. What says an item is one is neither the rarity nor the class ("Stackable Currency",
    shared with every orb in the game) but the **taxonomy block**, `Genus` / `Group` / `Family`,
    which nothing else in the game prints — hence `Item::is_beast()` reading `PropertyKey::Genus`.
    A beast's usage note is also the one place the game writes the instruction **hyphenated**
    ("Right-click to add this to your bestiary."), and a usage needle is a substring, so the
    "Right click" every other item prints never matched it and the line came back as a fourth,
    unrecognised modifier. Both spellings are needles now.
    An **Inscribed Ultimatum** is the third item whose header says nothing about it, and the one
    where it says least: its class is the "Misc Map Items" every invitation shares and its rarity
    line reads "Currency" exactly as an orb's does. `Item::is_ultimatum()` reads
    `PropertyKey::Challenge` — the trial it names, which nothing else in the game prints — and the
    `Requires Sacrifice` beside it is the second key this added. It needed no `is_gear()` rule:
    its class already makes it a map fragment, so the prose heuristics were never reached, and its
    thirteen hazard lines parse as ordinary modifiers. Which of them is *searched* is the plan's
    business, not the parser's.
    A **heist contract or blueprint** is the opposite: its item class ("Contracts", "Blueprints")
    is its own, so `ClassKind` carries it and `Item::is_heist()` reads that. What it hides is in
    the property block. **"Requires Brute Force (Level 4)" is the one property line the game
    writes as a sentence** — no colon, so it used to come out label-less with no key and no
    number — and it is kept exactly that way, whole and label-less under `PropertyKey::HeistJob`
    with the level in `num`, because that is how the panel already draws it and taking it apart
    into a label and a value would change what the reader sees. Which of the nine jobs it names
    is a lexicon lookup where it is needed, not a field. The reveal counts keep both of their
    numbers ("3/21") for the same reason. Its usage note is two sentences and neither opens with
    a click instruction, so **"Adiyah"** — the fence both are handed to — is the needle, and
    without it a unique contract's only "modifier" was the instruction to hand it in.
    An **itemised sanctum** names itself on the header line too ("Sanctum Research", another
    `ClassKind`), and what it hides is two things. Its state is properties — `Resolve`,
    `Inspiration`, `Aureus`, and the boons and afflictions as comma-separated lists of names,
    minor and major sharing a key each because the label is what says which and the *names* are
    what a search needs. And **its affixes are not explicit**: every sanctum stat the site
    publishes lives in a `sanctum.` namespace alone, and a wording is resolved against one
    namespace at a time, so with the ordinary fallback "The Merchant has 10 additional Choices"
    matched nothing at all — the mod section's fallback type is `ModType::Sanctum` on one.
    It is also the one item where a prose block's **position** is what tells it from a modifier:
    "The treasures within are tainted by a black spirit." sits between the affixes and the usage
    note, exactly where a rare's wordy modifier would, so `is_bottom_prose` takes the *last*
    prose block and only that one — several sanctum affixes carry no number ("Cannot have Boons")
    and taking the first would swallow them.
  - Mod type comes from the ` (implicit)` / ` (crafted)` / … suffix, else from an Advanced Mod
    Descriptions info line's generation words, else Explicit — except on a sanctum, above. A flask
    enchant carries no suffix, so on a flask the earlier of two unsuffixed sections is the enchant.
  - The info line's em-dash segments are tags **and** "— 20% Increased", which is a catalyst saying
    it scaled this mod. The clipboard prints the *unscaled* roll and range in that case (`30(20-30)`
    where the tooltip reads 36%), so `roll_incr` is applied to both in `match_stat`. A plain `" - "`
    separates them too, because a Latin-1 clipboard read degrades the em dash — see the clipboard
    seam in [platform.md](platform.md). Miss that and the line stays one blob: no tier, no tags, and `generation` never
    ends in "Prefix", so the affix is unknown.
  - Influence lines sometimes arrive glued to the end of the last mod block instead of in a section
    of their own; trailing flag lines are peeled off before the block is parsed as mods.
  - A **gem** has no rolled mods: its stat lines are `inherent_lines`, its skill text `description`.
    Three things are pulled out because they are the whole of what a gem is priced on.
    `gem_level` is the `Level:` in the **property block** — the clipboard prints that label twice
    and the one under `Requirements:` is the character level to socket it, a different number on
    every gem past the first. `transfigured` is a flag line like `Corrupted`. And `vaal_name` is
    the lone-line section heading the second half of a **Vaal gem**, which is two skills in one
    item: the *name* line prints the base skill ("Blight") and only that heading says this is a
    Vaal Blight. `Item::gem_name()` puts the two back together into the one name both markets
    file the gem under — the Vaal skill, or for a transfigured Vaal gem the pair the trade site
    and poe.ninja both write as "Vaal Blight (Blight of Atrophy)".
- **`item/resolve`** — needs the bundle. Two jobs the matcher cannot do alone:
  - **Local vs global.** The bundle keeps a second record for local stats, marked by a **`" (Local)"`
    suffix on the matcher string**; "20% increased Attack Speed" is the weapon's own speed on a
    weapon and the character's anywhere else, and the two have different trade ids. The clipboard
    never says which. `kLocalWordings` lists the 20 wordings that have a local twin and what the item
    must display for it to apply (a weapon, or the defence the wording names). Get this wrong and the
    price check silently searches the wrong stat.
  - **Affix ≠ stat.** An info line groups every line of one affix, but "+34 to Armour" and "+28 to
    maximum Life" from one prefix are two trade filters, so `split_affix` walks the group and lets
    the matcher say how many lines each stat takes (genuine multi-line stats exist — a cluster
    jewel's enchantment). Without info lines the same walk merges hybrid lines instead, driven by
    `StatMatch::lines_consumed`. Every part keeps the affix's name/tier/tags; `continuation` marks
    the ones that must not print the info line again.
  - **The gem's own record** (`resolve_gem`, `Namespace::Gem`), looked up on `Item::gem_name()`.
    What it is there for is `BaseType::trade_name`: **trade files a transfigured gem under the
    skill it alters**, so "Raise Zombie of Falling" is the type `Raise Zombie` with the `alt_y`
    discriminator, and a search naming what the clipboard printed matches *nothing* while the
    bare type matches the unaltered gem — a real, far cheaper item. The discriminator is also
    what tells two records under one key apart, which is the shape a bundle published before
    the display names existed has: three "Vaal Blight" rows, only one of them the plain gem. A
    transfigured gem is exactly the one with a discriminator, so nothing falls back to
    "whichever came first".
  - **A beast's species** (`Namespace::CapturedBeast`). An itemised beast prints two name lines
    and neither means what it does on a rare: the first is a title the capture generated
    ("Banebite the Malignant") that no two copies share, and the second is the **species** —
    a Wild Hellion Alpha — which is the base and the whole of what one copy has in common with
    another. The bundle files them in a namespace of their own, so looking one up among the
    ordinary bases finds nothing at all; the species is also the only thing a beastcrafting
    recipe names, which is why it is what gets searched.
  - **A card's own record** (`Namespace::DivinationCard`). Nothing about a card is *searched* — it
    is `Strategy::Currency` and the in-game exchange is the whole answer — but that answer is
    keyed on `BaseType::metadata_id`, which only a resolved base carries, so a card that fell
    through this had no base, no metadata id and therefore no price at all. An essence needed
    nothing: it is an ordinary `Namespace::Item` base and already resolved.
  - **Which uniques an unidentified one could be** (`Item::unique_candidates`, off
    `find_uniques_on_base`). An unidentified unique prints **one** name line and it is the
    base's, so the base is the whole of what the item says about itself and the bundle's
    base → uniques index is the only thing that turns it back into a name. **One candidate is
    not a guess** — that base rolls into exactly that unique — so it is taken, and everything
    downstream plans, prices and searches as if the item had named itself. Several is a
    question only the player looking at the art can answer (`Item::needs_unique_choice`,
    settled by `choose_unique`, which refuses anything outside the candidate list so a pointer
    from the previous item or from a swapped-out bundle can never be what gets searched).
    None is two different facts and `GameData::has_unique_bases()` is what separates them.
- **`item/derive`** — the numbers the game leaves implicit. **Quality scales the base's own inherent
  value and nothing else**: not the flat local rolls added to it, though the local *increases* then
  apply to both. One rule for a weapon's physical damage and for armour / evasion / energy shield /
  ward, differing only in which mods count as local:
  `displayed = (base * (1 + q/100) + flat) * (1 + incr/100)`, inverted by `inherent_roll`.
  Do **not** replace it with the additive `v20 = v * (120 + incr) / (100 + q + incr)`, which is what
  the reference tools compute and what "quality is additive with increased physical damage" describes:
  it recovers a base ~8% too high whenever an item carries both quality and a flat local roll, and on
  the Rift Carapace capture (`examples/item_6`) that puts its energy shield at 316.8 against a
  Twilight Regalia range of 262..302 — outside its own base, so the percentile is lost. The inherent
  form puts it at 293.3, the 78th percentile. That capture is the only real evidence either way; a
  before/after-quality capture of one item with a flat local roll would pin it down.
  Local increases and flat adds are found from the *wording* (`placeholder_form`), so they still count
  when the stat itself is missing from the bundle. **Base percentile** (`Derived::base_pct`) is
  `inherent_roll` placed in the base type's range; a result outside that range means a local mod was
  missed, and then it reports nothing rather than a confident 0%. It is **one number per item, not
  one per defence**: a base rolls a single value and spreads it over the defences it has, so the
  sum of the recovered inherent values goes into the sum of the base's ranges — an armour/energy
  shield hybrid whose two percentiles disagree is showing rounding, not two rolls. A defence with no
  published range makes the two sums incomparable, so there is no percentile at all. There is no
  weapon percentile: the bundle publishes defence ranges for armour bases but no damage ranges for
  weapon bases. It is a **filter and not a remark** — `armour_filters.base_defence_percentile` on
  the trade site — and it is **floored, never rounded**, because the filter is a minimum and a
  78.6th-percentile item asked for at 79 does not match itself. Ticked only on a `BaseItem` plan,
  where the base's roll is what is being bought; on a modifier search the defence totals already
  carry it, and asking the same question twice only drops the listings that answer it once.
- **`item/range_match`** — how wide a filter opens around the roll. It is the one input here that
  is a **setting rather than a fact about the item**, which is why it arrives from outside as a
  `RangeMatch` and `Config` is the only thing that owns one. Each side of the interval is
  `Unbound` (fill nothing), `Exact` (the roll), `Within` (a percentage of it) or `WithinTiered`
  (the same, gated by what the modifier's own tier can roll), and the default is **tier-gated 5%
  on both**: what a buyer wants is a copy that rolled about what theirs did, and a bound outside
  the affix's own tier can only drop the listings that answer the question exactly. Four things
  that are easy to get wrong.
  The window is **rounded outwards at the filter's own last digit** — floor the lower bound, ceil
  the upper — so rounding never asks for a roll the item in hand does not have, and any non-zero
  percentage moves the bound by at least one digit (5% of 20 is exactly 1, 5% of 1 is a twentieth,
  and both still move by one).
  The slack comes off the **magnitude**, so a negative roll widens outwards like a positive one:
  -11 opens to -12..-10 rather than to that pair read backwards.
  **`lower_is_better` swaps which mode governs which side**, because "Minimum" means the bound
  that says *at least this good* and on a modifier the game prints negative that is the upper one.
  It is invisible while the two modes agree — a symmetric window is symmetric either way round —
  which is why the tests state that case with one side `Unbound`.
  And the tier gate **never crosses the roll**: a legacy roll sits outside the range its modifier
  publishes today, and gating to that would ask for a copy of the item that is not this one.
  `WithinTiered` falls back to `Within` where no tier is known at all.
  It seeds **stat filters on every strategy** — the old split, where a `Modifiers` plan took the
  whole tier range and everything else took "no worse than this", is gone; both are now points on
  the same dial. It deliberately does *not* touch the numeric filters: those are thresholds on a
  total ("at least this much armour"), and a maximum on one rules out the strictly better items a
  buyer would still take.
- **`item/plan`** — `SearchPlan`: strategy, category/name/type, corruption, influences, stat filters
  and numeric filters, plus **`notes` for everything deliberately left out**. Strategy decides what
  matters: `Modifiers` (magic/rare) enables every mod and seeds its bounds off the roll it made
  (how wide is `item/range_match`, above), and names no base — **except on a flask**, whose base is
  half of what its mods are worth (the same suffix is a sought-after roll on a Quicksilver Flask
  and nothing on a Ruby one, and trade files every flask under one category, so the `type` is the
  only place to say which). Only ever off a **resolved** base: an unstripped magic name
  ("Surgeon's Quicksilver Flask of the Cheetah") as the `type` matches nothing, which reads as
  nobody selling one, so an unknown base is a note instead. `BaseItem` (white, or a rare the user
  switches over) searches the base with item level and influences and enables only fractured mods and non-inherent
  implicits; `Unique` searches the name and enables a roll the **per-unique modifier data** says comes
  from a pool (see [strategy-unique.md](strategy-unique.md)), a roll a range proves is variable, any mod *added* to the unique —
  `{ Foulborn Unique Modifier }`, i.e. `Modifier::added_unique()`,
  which not every copy of that unique carries — and anything the player *crafted onto this copy*
  (`added_to_copy`: enchant, crafted, fractured, scourge, veiled, crucible). An enchant costs
  currency and is most of what an enchanted copy sells for, so leaving it out prices a different
  item. A `Maps` item class is `Map` at every rarity it prints — see
  [strategy-map.md](strategy-map.md). A **map
  fragment** (scarab, ember, splinter, invitation) is `Currency` whatever its rarity line says —
  see [ninja.md](ninja.md).
  **A number that is not a roll is not a bound.** A fixed modifier says the same thing on every
  copy of itself, and asking the trade site to compare its number asks it to compare a value it
  does not index the stat on — which matches *nothing*, so the price check comes back empty and
  reads as an item nobody wants. Measured, not inferred: a map's Baran implicit ("Item Quantity
  increases amount of Rewards Baran drops by 20% of its value") returned **0 listings with
  `min: 20` against 1705 without it**, and "Area is influenced by The Elder" — whose number is
  not in the clipboard at all, but the constant `StatMatcher::value` substitutes for the
  influence — **0 against 10000**. The filter stays and only its number goes, so the search asks
  for the modifier being present, which is the only thing a fixed modifier can be asked about.
  What says a number is fixed is that the game printed **no range beside it**, and that is only
  evidence on an item that printed ranges *somewhere* — with Advanced Mod Descriptions off
  nothing carries one, and reading their absence as "fixed" would strip the floor off every real
  roll and search a rare for "has a life modifier". Hence `ranges_printed`, asked of the whole
  item, because the setting is a property of the owner rather than of the modifier. A **map**
  needs no such evidence and is exempt: no number one of its implicits or enchants carries is
  ever a roll.
  **A tier or a rank is itself a range**, printed or not, and outranks all of the above: a
  different tier is a different number, so "no worse than what this one gave" is a real question
  even where the tier holds a single value. It is also the only way an **eldritch implicit** can
  say its number moves — its magnitude comes from the tier of the currency that put it there, so
  the clipboard has no range to print and states the rank instead
  (`{ Eater of Worlds Implicit Modifier (Lesser) }` → `Modifier::qualifier`).
  **Where the bundle knows a range the clipboard does not print, the bundle wins**:
  `apply_unique_mods` restores the bound the moment the per-unique data says the modifier rolls,
  because a range is a range whichever source stated it. The one thing that does *not* work in
  reverse is a record calling a modifier fixed — the item's own printed range outranks a record
  about the unique in general.
  A filter with one side left open asks for "no worse than this", and **worse is not always
  smaller**: a mod the game prints negative is better the more negative it is (an eldritch implicit
  applying `-11%` to Cold Resistance — its magnitude comes from the currency tier, so the clipboard
  prints no range to bound it with), and so is a stat the bundle marks `better: -1`. Both put the
  open side at the top, which is what `seed_bounds` swapping the two modes does. The sign is what
  carries the direction for the rest, because the canonical wording
  already does — "#% reduced Mana Cost" is stored as a negative increase. It reads wrong only for a
  negative roll of a stat that also rolls positive, i.e. a resistance penalty, which is a drawback
  on a unique rather than something a buyer searches for.
  Two rules that are easy to get wrong: trade indexes **repeated stats as their total**, so
  `merge_same_stat` sums two life rolls into 104–117 rather than filtering twice; and an
  added-damage mod is indexed as **the average of its two numbers** while every other multi-number
  wording is indexed on its **first** ("15% chance to Unnerve … for 4 seconds" is searched on 15,
  not on 9.5) — hence `StatMatch::roll_bounds` being per roll.
  The **weapon numerics** are the three DPS totals, plus attacks per second and critical strike
  chance — and those last two are ticked **only where the game printed the property augmented**,
  i.e. where a modifier on this copy raised it above the base's own. Every weapon has both numbers
  and on most of them they are the base's, so asking for one rules out nothing but the same weapon
  in somebody else's stash. The augmented marker is the whole of the evidence: the bundle publishes
  no base crit chance or attack speed to compare against.
  **A modifier already inside a searched number is not searched again by name**
  (`unimpose_derived_mods`, off `derived_filter_keys` in `item/derive`). A local roll is not
  something the item has beside its armour — it *is* part of the armour the item displays — so a
  query carrying both the number and the modifier behind it asks one question twice, and the
  second asking is the brittle half: a flat roll and a local increase reach the same armour by
  different routes, and naming this item's route rules out every other way of arriving at the
  number the buyer wants. So the derived value is imposed and the modifier is only offered —
  *left* in the list, not removed. It is conditional on the derived filter actually being
  enabled: on a unique, where the defences and DPS are offered rather than imposed, the modifier
  is all there is to ask about.
  **A fractured roll is the exception and keeps its filter.** It cannot be re-rolled and it is
  what survives every craft the buyer will do afterwards, so *which* modifier reached the number
  is the point of the item rather than an over-constraint on it — and trade indexes it in a
  namespace of its own (`fractured.stat_…`, which `to_filter` already sends off `Modifier::type`,
  alongside the item-level `misc_filters.fractured_item`), so it is a different question from the
  same wording rolled ordinarily. A crafted roll gets no such exemption: a bench craft is
  something any buyer can add.
  **Locality is the whole of it** and is decided in `item/derive` rather than in the data, from
  the same wordings and the same guards `sum_locals` uses — "20% increased Attack Speed" is the
  weapon's own only on a weapon, and "#% increased Energy Shield" the item's own only where the
  item displays energy shield. Attack speed feeds `aps` *and* all three DPS numbers; added
  elemental damage feeds `edps` and `dps` but not `pdps`; "#% increased Elemental Damage" feeds
  none of them, because it never touches what the weapon displays. The base percentile is the one
  derived number a local roll is **not** inside: it is recovered by taking those rolls back out.
  **Everything the site takes as an `{"option": …}` is a `SearchPlan::options` entry** — the
  booleans (corrupted, mirrored, foulborn, identified, blighted), and the closed vocabularies (a
  chart's shape, a Valdo map's payout). One struct, because the wire form is one thing and only the
  source of the string differs; `option_group_for` in `trade/query` is what files each under
  `misc_filters` or `map_filters`.
  The rule for the booleans is one line: the search
  asks the item to be what it is, and it says so out loud only where that is not the ordinary
  answer. `OptionFilter::shown` is the whole of the struct's reason to exist — an uncorrupted,
  unmirrored, unmutated, identified item is what nearly every check is about, so those four are
  imposed with no row at all, and four rows saying nothing is unusual would push the modifiers off
  the panel. The *unusual* value gets the row, because that is the one a buyer might want to widen
  back out: a mirrored item cannot be crafted on, an unidentified one is a different product, a
  corrupted one is a different market. Synthesis and fracturing are asked in one direction only —
  evidence about the copy in hand rather than a choice, and an ordinary item's search has no
  reason to rule out the strictly more constrained copies. **`identified` is not asked of a gem
  or a currency item**, measured rather than assumed: `identified: true` returns **0 listings**
  under `category: gem` and **0** for a Facetor's Lens, against 10000 and 177 without it, because
  trade indexes the flag only for what can be unidentified. `mirrored: false` was checked the
  same way and is safe everywhere.
  **Foulborn is one of those booleans and the site's key for it is `mutated`.** Chayula's
  mutation is a different item at a different price — measured on Tulfall: 3855 listings in all,
  1896 not foulborn and 1960 foulborn, and the mutated ones *cheaper* — so a search that leaves
  it open prices the two markets together and undercuts the copy in hand. Nothing about it is a
  flag line: the game states it as a prefix on the name ("Foulborn Romira's Banquet") and as the
  info line of the modifier it added, and `parse_item` takes either, the name being the half that
  survives Advanced Mod Descriptions being off. `mutated: false` is safe everywhere `mirrored`
  is, checked the same way (655/655 gems, 1299 Facetor's Lenses, 10000 wands and 10000 tier-16
  maps either way), so it is imposed at every strategy even though only a unique can be one.
- **`item/plan`'s three property filters** (`add_property_filters`) are the `misc_filters`
  intervals that come off a **property line** rather than off a modifier, so none has a tier to
  gate against and none gets a window: the number is what this copy has, and all a filter can say
  is "no worse". **Which side that leaves open is the judgement**, and it differs per property.
  **Memory Strands** (1–100) are spent to raise the tier of a modifier a craft adds, so more is
  more of what is being bought — a floor, ticked. **Intangibility** is the opposite: the penalty
  an item accrues from Allflame crafting, the chance the next craft comes back with one outcome
  instead of several, so less is better and it is a **ceiling** — left unticked, since a buyer
  who will not craft on the item does not care what it accrued. **Stored Experience** is the one
  thing telling two copies of a Facetor's Lens apart.
  That last one is why **the Facetor's Lens is the one currency item with a trade search**. Every
  copy holds a different number, so they are listed individually rather than traded by the stack,
  and naming the type is all a search needs — the same shape as the map fragment that prints an
  item level: what says a currency item is not interchangeable is that it prints something no
  other copy of it does. The strategy stays `Currency` (poe.ninja still prices it in the currency
  market, which is the floor under the search) and `trade::searchable` reads the `type` the plan
  filled in, exactly as it does for a gem.
- **`item/plan`'s beast strategy** (`plan_beast`) is the shortest one here, and every line of it
  is a thing not asked for. An itemised beast is bought to be released into the menagerie and
  spent on a beastcrafting recipe, and a recipe names the **species** and nothing else — so the
  plan is the species as the `type`, the item level as a floor, and that is all.
  The title above the species is left out because no two copies share one. The **monster
  modifiers** are left out for the same reason a map's affixes are, and just as silently: they
  are the captured monster's own abilities rather than rolls on a base, the bundle has no stat
  for "Satyr Storm" to match, and no recipe asks for one — so "unrecognised modifier: Evasive",
  five times over, would charge the check with failing at what it did on purpose. The item level
  is a **floor** rather than a window: a recipe wanting one wants at least that much, and a
  higher beast still answers.
  The one thing that is not a matter of judgement is the **category**, and it is the single
  place a category is not the bundle's answer for the item class. A beast's class is "Stackable
  Currency" and the bundle maps that to `currency`, which is right for every orb that prints it
  and wrong here: trade files beasts under `monster.beast`. Measured, because the site accepts
  either and a wrong category comes back as nobody selling one rather than as an error —
  `currency` returned **0 matches** for a Wild Hellion Alpha against **1602** for
  `monster.beast`, same type and same item level.
- **`item/plan`'s ultimatum strategy** (`plan_ultimatum`) prices a **contract**, and the search
  asks for the same contract rather than for a better one. An Inscribed Ultimatum is bought to
  stake a thing and win a thing, so the plan is the four `ultimatum_filters` options — the trial,
  the reward type, the item sacrificed and, where the reward is a unique, that unique — plus the
  area level, plus exactly two of the thirteen modifiers.
  **The two exact modifiers are the whole judgement here.** "#% increased Monster Damage" and
  "#% more Monster Life" are not rolls to be beaten: they are the scale of the deal, and on a
  currency or divination-card ultimatum they say how large the stake is. 200% more Monster Life
  *is* the ultimatum that stakes eight Divine Orbs, so a filter reading "at least 120%" prices the
  four-orb ones alongside it. They are pinned to their own value **whatever the range-match
  setting says**, which is the one place a plan overrides that setting outright, and it is done in
  `build_plan` rather than in `to_filter` because it is the strategy that makes it true.
  Everything else the item prints — Choking Miasma, Drought, Shattered Shield — is left out, and
  as silently as a map's affixes: those lines are the shape of the danger, they sit on the item
  card beside the panel, and eleven notes would charge the check with failing at eleven things it
  left out on purpose.
  Three joins are tables rather than guesses. The **challenge** is matched against
  `TermList::UltimatumChallenges` and sent as trade's own option id (`Conquer`), the same shape
  as a chart's shape and for the same reason — the game prints the option's own words, so the
  entries are the site's text and the comparison is **case-insensitive**, because the client
  writes "Defeat waves of enemies" where the site writes "Defeat Waves of Enemies". All four
  trials are pinned to a capture, which is what the case rule rests on: the ids (`Defense`,
  `Survival`) resemble the wordings ("Protect the Altar", "Survive") closely enough to guess from
  and not closely enough to derive. The **reward**
  is `TermList::UltimatumRewards`, which has three entries for four rewards: the fourth is a
  unique, and the line the game prints for it is that unique's name, so a wording that matches
  none of the three is looked up as a unique and becomes `ExchangeUnique` plus an
  `ultimatum_output`. The **sacrifice** is looked up across uniques, cards and currency — the
  three namespaces the site's own `knownItem` filter names — and only a confirmed name is ever
  sent, because trade fails the whole search on a required item it does not know, exactly as it
  does on a Valdo map's reward. The count after it (`Divine Orb x8`) is dropped: trade indexes no
  such number, and the two modifiers above already imply it. The mirror reward is the one with no
  nameable stake at all — "Mirrorable, Rare Item" is a class of items, and the reward type has
  already said so — so nothing is asked and nothing is apologised for.
  The **category is dropped outright**, which is the second override of the bundle's answer for
  an item class and the first where the override is to send nothing. "Misc Map Items" maps to
  `map.fragment`, right for the invitations and splinters sharing the class and wrong here.
  Measured, for the same reason the beast's was: **0 matches** with it against **443** without,
  everything else identical. Nothing is lost — an ultimatum is one base type, so the `type` term
  already says everything a category could, and `trade::searchable` asks for that type **and** at
  least one ultimatum filter, because the type alone is every ultimatum in the league.
  All four options are `shown` and ticked. Unlike the booleans, none of them is the ordinary
  answer that would be noise on a row: they are the terms of the deal, and which of them to relax
  is exactly the question the reader is there to answer.
- **`item/plan`'s heist strategy** (`plan_heist`) covers contracts and blueprints together — they
  share every filter the site has for them and differ only in the category and in whether reveal
  counts are printed. It is the **first iteration of a market nobody here has traded**, so it errs
  towards offering rather than deciding: every heist filter the site publishes is a row, and one
  question decides the tick. *Is this a fact about which item this is, or is it the variation
  between two copies of the same one?*
  Imposed, because they say which run it is: the **area**, which is the base type and what the
  game names the item after ("Blueprint: Underbelly") — a rare heist item's own name, "Cataclysm
  Vow", is generated per copy and no more searchable than a rare bow's; the **area level**, exact,
  on the same reading a chart's and an ultimatum's get; **what is revealed** on a blueprint, as a
  floor, with the **total** beside it exact where the site indexes one, because a blueprint's wing
  count varies per copy and a four-wing Tunnels is a different item rather than a better one; the
  **objective's value** on a contract, which is the parenthetical after the target and the only
  thing about that target the site takes; and the **enchant**, which on a blueprint is what the
  whole run is for and is something somebody paid to put there.
  Offered and left unticked: the **job levels**, seeded as ceilings, because a requirement is a
  demand on the *buyer's* rogue rather than a property of the thing being bought, and a copy
  asking less is strictly more usable; and the **heist modifiers**, which are the danger the run
  will hold — rolled, re-rollable, the map argument exactly, except that here the row stays and
  only the tick goes, because a contract carries seven and ticking all seven asks for one
  particular copy in the world.
  Two things are left out entirely. **Item quantity, item rarity, alert level reduction, time
  before lockdown and maximum alive reinforcements** have no `heist_` filter at all, so there is
  nowhere to put them. And **Total Escape Routes** has one, `heist_max_escape_routes`, which the
  site accepts and indexes no listing under — so any bound at all empties the result, and it is
  not offered as an unticked row either, because ticking it would do the same. Measured one
  filter at a time on the fully revealed Tunnels capture, everything else identical:
  `heist_max_wings` at 4 returned **460** and `heist_max_reward_rooms` at 28 returned **460**
  (1040 at a bare `min: 1`), against **0** for `heist_max_escape_routes` both at the item's own 8
  and at `min: 1`. The first pass measured all three totals together and concluded the whole
  family was dead, which is what one variable at a time exists to prevent.
  A **unique** contract is left to the unique strategy: it is bought for its name, and the name
  fixes everything else about it. Trade files it under the name the clipboard prints, `"Contract:
  The Slaver King"` — prefix and all — so nothing is stripped off either name line.
- **`item/plan`'s sanctum strategy** (`plan_sanctum`) prices a **run in progress**, and it is the
  one strategy here where nothing is left unticked on the argument that it is a roll. A sanctum
  is "Unmodifiable" — no currency can be applied to it again — so its affixes are as much a part
  of what is being bought as its resolve is, and there is no re-rollable half to hold back.
  Imposed: the **floor**, which is the base type (an Archives run and a Vaults run are different
  items); the **area level**, exact, the reading a chart's, an ultimatum's and a heist item's
  already get; **Resolve**, **Inspiration** and **Aureus** as floors, since more of each is
  strictly more of what is being finished; **every boon and affliction**; and the **affixes**,
  through the ordinary mod path.
  The boons and afflictions are the interesting join. They are printed as a **property** — a
  comma-separated list of names under `Minor Boons:` / `Major Afflictions:` — and each of them is
  a stat of its own, `sanctum.sanctum_effect_…`, whose wording is the name with a `Has ` in front
  ("Has Rusted Chimes"). So the filters are built in `plan_sanctum` rather than by `to_filter`,
  with no `mod_index` behind them, and the prefix is a lexicon entry (`Term::SanctumEffectPrefix`)
  because a translated bundle translates the stat too. A name the bundle has no stat for is left
  out and said out loud, the way an unknown beast species or ultimatum stake is: the effect list
  grows with the league.
  **Maximum Resolve is the one row seeded and left unticked.** Resolve is printed as `299/300` and
  both numbers are read, but the second says more about the character that opened the sanctum than
  about how much run is left to sell — so it is offered open on the right and the tick is the
  reader's.
  Everything here was verified live one filter at a time, with an impossible bound in place of the
  item's own value: `sanctum_resolve`, `sanctum_max_resolve`, `sanctum_inspiration`, `sanctum_gold`
  and `area_level` each took the result to **0**, and adding one effect stat the item does not
  carry did the same — so all of them are indexed, `sanctum_max_resolve` included, and none of
  them is a `heist_max_escape_routes`. What could **not** be measured is how narrow any of them
  is: the whole `sanctum.research` category held **1** listing in the league at the time, so
  every count from "type and category alone" up to the full eleven-filter plan was the same 1.
- **`item/plan`'s unique strategy** — the per-unique modifier join (`apply_unique_mods`) and
  the unidentified case (`plan_unidentified`) are [strategy-unique.md](strategy-unique.md).
- **`item/plan`'s map strategy** (`plan_map`, `plan_chart`, `add_map_pseudo`) is
  [strategy-map.md](strategy-map.md) — maps, charts and Valdo maps, and the one strategy that
  searches on none of an item's affixes.
- **`item/plan`'s gem strategy** (`plan_gem`) is [strategy-gem.md](strategy-gem.md).
