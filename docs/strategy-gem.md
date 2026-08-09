# The gem strategy

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

The shortest search here. How a gem's name is resolved is under `item/resolve` in
[item-layer.md](item-layer.md), and its reference price is in [ninja.md](ninja.md).

**`item/plan`'s gem strategy** (`plan_gem`) is the shortest search here and the only one whose
filters are *all* numeric: the name, `misc_filters.gem_level`, `misc_filters.quality`, and the
corruption every strategy already matches exactly. Everything a gem prints is what the skill
does and is identical on every copy of it, so there is nothing to turn into a stat filter and
nothing to leave a note about either.
**Level and quality are exact — `min == max`, the same reasoning as a map's tier.** A level 21
gem is not a better level 20 one, it is what the gem sells as; a floor would put 21/23
corrupted gems into the results for a 20/20 and price a different item. Quality is filtered at
zero as readily as at twenty, because an unquality gem is a different thing from a 20% one and
no filter at all prices it as whichever quality is cheapest. Corruption is the hard split
underneath both: it is what allows level 21 and quality 23 to exist.
The name is the **record's**, never the printed one — see `item/resolve` in
[item-layer.md](item-layer.md) and
`Item::gem_name()` — and a gem the bundle cannot name gets **no search at all**, only a note.
That is `trade::searchable` returning false on an empty `type` for this strategy alone: every
other strategy still has modifiers or a category to fall back on, while a gem falls back to
every gem in the game at this level, whose cheapest listing would read as this gem's price.
poe.ninja still prices it, so the check is not empty. In practice this only happens on a
bundle older than `data-20260807.23`, the release that keys gems on their printed names, and
then only for transfigured gems.
