# Fix/change now and commit

The price check window is too crowded on screens with 1080p resolutions or lower.

In the last phase we added a gutter to the price check window for showing search result tooltips.

Would it be possible to render the clipboard item also in this gutter, at the top, instead of above the filter section? The filter section will therefore become the top of the price check window and below it would be the poe.ninja price and the search results. Independent of resolution, we should have more real estate horizontally (to the left/right) than vertically.

On windows, the app opens a console window instead of just running in tray. That is not a desirable outcome.

The ?PPC frame that you render when you detect an active Path of Exile window is obsolete but I wouldn't get rid of it. We could place a status (see below) to the center of mana globe, very faintly opaque (like 50% transparency) that would show the status. Yellow font with either a black or white outline (if possible) without any border or background, just text, would be awesome.

---

PoPC vM.m.r
{data version}
---

Two lines, first line with PoPC and current version, second line with data bundle version.

If you need to look up where to place it approximately, see @tests/data/full_screen_1440p.jpeg, aim for the middle of the mana globe (blue)

# 2nd commit of 2nd phase

poe.ninja lookups - map which categories can and should be looked up with poe.ninja and display the ninja price (respect currency in which PoE.ninja reports the price, you should have currency glyphs already saved and cached from trade queries. Format the price the same as trade lookup 5 x [DivineIcon] Divine Orb)

Depending on feasability, include the price development graph and trend for the last 7days that poe.ninja presents. On the website, it's an SVG.

The row, for which we now have a placeholder, should look like this (three columns, | is column separator just for you don't actually render that):
{poe.ninja logo} | [CurrencySymbol] {price} | {trend graph} {trend value%|red for downward green for upward trend}

If the row could be clickable to open the item's poe.ninja page, it would be amazing. If you choose to render it with a table don't include a table heading, just the row with data.

You should periodically fetch and remember currency ratios (especially divine to chaos), validity for cache should be 30m. You can cache everything you fetch out of poe.ninja with a 30m lifespan for that matter in case of repeated lookups for same item.

If it would be cheaper / easier to fetch and keep all locally, do that. If not, just periodically fetch and keep currency ratios (not automatically in background but when requested for price check and cache is stale, refresh), everything else fetch and cache on-demmand (when pricing a unique, a gem, or anything matching poe.ninja price information)

!Pause after you've commited the first part so I can compact or clear the context before you continue. If you have any questions ask them before you start and note them down into this design document.

---

# Questions asked before starting (and their answers)

1. **The gutter now holds two items — where does the hovered listing's item go?**
   *Answered: stacked below.* The clipboard item is pinned at the top of the gutter for the
   whole check; a hovered listing's item is drawn under it, top-aligned to its own row but never
   above the pinned item's bottom edge, and clamped so it does not run off the screen. Both are
   on screen at once, which is the comparison the hover is for.
2. **How is the mana-globe status marker positioned?**
   *Answered: config-file knobs.* Two fractions in `config.json`, not in the Settings UI (same
   treatment as `poe_window_title`), defaulting to what was measured off
   `tests/data/full_screen_1440p.jpeg`. They are offsets from the game window's **bottom-right
   corner divided by its height**, because PoE scales its UI with height — the same reasoning as
   `stash_edge`/`inventory_edge`.

Decisions taken without asking, for the record:

- **No gutter, no card.** On a game window too narrow to spare a readable gutter (the existing
  `kMinGutter`, 260px) the item falls back to the top of the panel exactly as before. The gutter
  is only what the game leaves free, so there is not always one.
- **The second status line** shows the installed bundle's version, or `no data` / `updating…`
  when there is none — a version line that is blank while the bundle downloads reads as broken.
- **A click on the item card does not dismiss the panel.** It is our own opaque UI now, not the
  transient tooltip the gutter used to hold, so `poll_click_away` has to know about it; a click
  on the transparent part of the gutter still dismisses, because that is a click on the game.

# Notes from the second commit (poe.ninja)

Decisions taken without asking, for the record:

- **The API had moved.** `poe.ninja/api/data/currencyoverview` and `itemoverview` are 404 now;
  PoE 1 is under `poe.ninja/poe1/api/economy/`, in two feeds with different payload shapes. The
  docs also state outright that only the *economy* endpoints are public and that breaking
  changes can happen without notice, so every field is parsed as optional.
- **It fetches on its own, not on a button.** It costs no GGG request, and the overviews are
  shared by every check and refreshed at most twice an hour, so making the user press something
  to see a reference price would be friction with nothing behind it. A trade search stays on its
  button because that one does spend the user's rate limit.
- **A unique's variant is matched on its modifiers**, which the notes did not ask for but which
  is the difference between a right price and a ten-fold wrong one: Ralakesh's Impatience is
  three lines on poe.ninja at 805, 133 and 75 chaos, and the copy in hand names which it is.
  Where the modifiers cannot separate them (Mageblood's flask count) the row states the span and
  the count instead of picking one, and the click-through settles it.
- **The trend graph is in.** `sparkLine`/`sparkline` is seven daily samples plus the percentage,
  which is exactly the SVG the site draws, so it needed no scraping.
- **A 30-minute cache on everything, as asked**, which happens to be what poe.ninja sets on its
  own responses — plus a conditional request when it expires, so an unchanged overview costs a
  304 rather than four megabytes. The divine rate rides in the currency market's payload, so it
  is not a separate thing to remember: it is fetched with the first check of every half hour.
- **A white, magic or rare item is priced as its base type** (asked for after the first pass).
  poe.ninja splits bases on three things and this matches all three: the base, the item level
  (bracketed, 82 up — below that there is no price at all) and the influences (exactly; Searing
  Exarch and Eater of Worlds are excluded, being implicit-borne rather than base influences).
  It carries no quality on a base line, so quality is not part of it. On a rare the row says
  outright that this is the bare base and the search below is what prices its modifiers.
- **A map is the only strategy with no row at all**, since poe.ninja prices one on its tier and
  nothing in the item layer gets that far.
