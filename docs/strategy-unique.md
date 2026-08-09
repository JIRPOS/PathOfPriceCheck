# The unique strategy

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

How `item/plan` searches a unique, and what it does when the copy has not been identified yet.
What every strategy shares is in [item-layer.md](item-layer.md); the dataset behind the join is
[UNIQUE-MODS.md](../UNIQUE-MODS.md).

## The per-unique join

**`item/plan`'s per-unique join** (`apply_unique_mods`) is what makes a unique searchable at all.
A unique's modifier can be variable **without printing a range**: Ralakesh's Impatience rolls one of
three charge mods, each `1..1`, and the clipboard prints that exactly like the four every copy has.
The bundle's `en-unique-mods.ndjson` says which mods are fixed and which come from a pool, so a
pooled mod is enabled and labelled with the pool's own prose ("Random charge modifier"), and a mod
the record calls fixed is now left out *knowingly* instead of with a warning. Three rules:
**join on the trade id, never on the wording** (wordings are shared by two stat records, which is
exactly what the ids disambiguate); **never disable** — the item's own printed range outranks a
record about the unique in general; and **only trust a range that contains the roll**, because the
bundle carries no `dp` for every stat and a range can arrive 100× the roll it bounds
(`0.4% of Physical Attack Damage Leeched as Mana` against `40..40` — see `examples/item_3`), which
would otherwise call a fixed mod variable. Pool membership is a fact about the item rather than a
number, so it survives that check. A mod the record does not have is reported, never dropped
silently: it is either something added to this copy or a mod the source has not caught up with, and
both are what a buyer is searching for. **Except a mod `added_to_copy` covers** — the record
describes the unique, not what was crafted onto one, so its absence there is by definition and
"not in the modifier data" reads as a failure to recognise a modifier that is right there in the
filter list.
**Reported on the row and not underneath the list** (`StatFilter::caveat`, a hover tooltip):
the row already names the modifier and shows its box unticked, so a note is that wording a
second time — three lines of panel each. Triad Grip is the case that proved it: its four
conversion modifiers are unlisted in the record *and* printed on the item, so both note
families fired for each and twelve lines went on saying what four unticked boxes had said.
So `UniqueMods::unlisted` is a caveat on the row wherever the item actually prints that
modifier, and stays a **note** only for prose with nothing on screen behind it ("4 random
Charm modifiers") — which is the case the note exists for, since there is no other way to
say the app is leaving something out.
[UNIQUE-MODS.md](../UNIQUE-MODS.md) is the dataset's contract, including what it does not cover.

## An unidentified unique

**`item/plan`'s unidentified unique** (`plan_unidentified`) searches the name `item/resolve`
worked out from the base, plus the **item level** — a floor, ticked, because it is the one
number an unidentified copy carries and it bounds what the item can still turn out to have
rolled. The `Unidentified` flag itself needs nothing new: `add_item_flags` already asks the
item to be what it is, and an unidentified one is exactly the case that flag has a row for.
Everything else about the item is behind the identification, so there is nothing else to
carry. Where the base rolls into **several** uniques and the user has not picked one yet,
there is no name and therefore no search — `App::can_search()` is false, the panel asks
instead (`draw_unique_choice`, see [trade-layer.md](trade-layer.md)) — and the plan states which question is open rather than running a search
for "some unique of this base", whose cheapest listing would read as this item's price.
Two notes ride along: the name the app took for itself when a base had a single unique, since
nothing on the item printed it, and that a reference price is what *identified* copies sell
for — poe.ninja does not split a unique by that, and an unidentified one is the gamble on the
rolls rather than the rolls.
