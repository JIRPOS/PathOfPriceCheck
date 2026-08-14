# Build & test

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # first run fetches + builds SDL3 (slow, cached after)
cmake --build build -j
ctest --test-dir build                         # all tests
ctest --test-dir build -R <name> -V            # a single test
./build/PathOfPriceCheck                       # run (add PPC_DEV_OVERLAY=1 to see the UI w/o PoE)
```

SDL3 builds from source, so **Linux needs dev headers**: `libx11-dev libxext-dev libxrandr-dev
libxcursor-dev libxi-dev libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols
libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev
libcurl4-openssl-dev zlib1g-dev` (the CI
workflows install exactly these). zlib is `ppc_core`'s only link dependency besides
nlohmann/json — it is the deflate behind `util/png`, and on Windows it is already in the tree,
fetched alongside curl. Windows needs only MSVC. The CI still validates the Windows build on
every push/PR — trust it for the Win32 platform code, which can't be compiled locally here.

The bundled font data is committed, so a normal build needs nothing extra. To change the typeface:
`./scripts/fetch-fonts.sh` (downloads the TTFs into the gitignored `assets/fonts/`) then
`./scripts/gen-font-data.sh` (rewrites `src/fontin_data.inc`). To change **which UI glyphs are
bundled**, the same pair one layer over: add the codepoint to `scripts/fetch-glyphs.sh` *and* the
name to `src/ui/glyphs.hpp` — they are a contract, and only one side of it draws anything — then
`./scripts/fetch-glyphs.sh && ./scripts/gen-glyph-data.sh`. That one needs `fonttools` for
`pyftsubset`, which is the only build-time dependency here that is not a system package.
The icon is the same deal — after
changing `assets/popc_icon.png`, run `./scripts/gen-icon-data.sh` (rewrites `src/icon_data.inc` and
`assets/popc_icon.ico`; needs ImageMagick for both, since the embedded copy is downscaled to 64px).
All three write plain byte arrays through `scripts/bin2c.py`, and
[architecture.md](architecture.md) says why they are not compressed.

`XDG_CONFIG_HOME` is worth setting for any dev run that touches map check: the rating tables are
files under `<config>/map-profiles/`, and a run with `PPC_DEV_MAP` writes a `Default.json` into the
real configuration directory otherwise. Pointing it at a scratch directory is also how a profile
with verdicts already in it gets in front of the popup, since there is no way to click one from a
script.

`-fsanitize=address,undefined` for debug builds is not wired into CMake yet; pass it by hand:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```

Note that a background job started from a non-interactive shell inherits `SIGINT` **ignored**, so
`kill -INT` will not exercise the shutdown path — launch it in a way that restores the default
disposition, or you will misread "still running" as a hang.

The item parser must be runnable and tested without any windowing or network dependency; that is
what `ppc_core` is for.

## Regenerating the test fixtures

`tests/parse_en.hpp` is where every test states that its captures came from an English client —
`parse_item` takes a lexicon and has no default, so the alternative was saying so at three hundred
call sites. `lexicon_test` covers the other direction with a hand-written pseudo-French lexicon and
an item written in it: nothing in that capture is English, so anything the parser still gets right
it got from the table, and the same bytes read as English are correctly not an item at all.

`tests/data/stat-normalization-vectors.ndjson` and `tests/data/bundle/` are slices of a real data
release, committed so the suite runs offline. **`./scripts/slice-test-bundle.py
../PathOfPriceCheck-Data/out` regenerates the bundle slice**; adding a case means adding a key to the
lists at the top of that script, never writing a record by hand. It copies every record verbatim and
rebuilds the indices from the offsets it just wrote, which is the point: the `.index.bin` files
address the ndjson by byte offset, so one extra byte per line silently shifts every record out from
under every lookup and fails as null lookups rather than as a diff. Keep the ndjson **LF and
byte-exact**; `.gitattributes` pins that down and those entries must stay. A key naming more than
one record is refused rather than resolved, so a unique that drops on two bases is asked for by
both — `"UNIQUE::Stormblood::Topaz Flask"` — and an item class two game classes print the name of
by its id, `"Maps::MapKey"`.

The slice has **no `(Local)` stat record**, so the local/global disambiguation in `item/resolve` is
not covered offline — it is verified against an installed bundle by hand. Adding one such record (and
one weapon base) to `STATS`/`ITEMS` in the slicer would close that gap. `tests/data/examples/` and
`tests/data/items/` hold clipboard captures for the parser; those are plain text and need no byte
discipline.

The two chart records are a pair on purpose: `ITEM::Coral Reef Chart` is what the clipboard's
base line says, and `ITEM::SeafloorRidges` is what trade files the chart under — under its
internal id, with the `chart` discriminator and no display name on it at all, which is the whole
reason `chart_area_key` exists.

The **logbook** entries are the only `pseudo.*` records in the slice that are looked up by
wording: two faction stats and five area stats, plus the seven destination implicits and the five
affixes a rare logbook prints below them. Two of those five affixes are there and still do not
resolve, which is a data-side gap rather than a slicing one — `+#% Monster Chaos Resistance` and
`+#% Monster Elemental Resistances` are published with the sign *inside* the matcher, and
`placeholder_form` replaces the sign along with the digits, so nothing the clipboard prints can
reach them. 34 of the bundle's 15,148 matchers are shaped that way. The pricing case asserts three
hidden affixes for that reason and becomes five the day it is fixed.

The slice's four `GEM::` records need a bundle from `data-20260807.23` or later, which is the
release that keys gems on the name the game prints. The transfigured one
(`Raise Zombie of Falling`) is the whole point of that field and is the record to check after
any re-slice: it is the only one carrying `tradeName`, and on an older bundle it does not exist
under that name at all.

Two records in it are there for their **`metadataId`** rather than for anything a search does with
them — `DIVINATION_CARD::The Blazing Fire` and `ITEM::Weeping Essence of Hatred` — because that
field is the only key the in-game exchange states an item by, and it only reaches the app through a
resolved base. They carry the **`exchange`** flag for the same reason, and the slicer copies
`source.exchange_items` out of the source bundle's manifest so the fixture can say the flags are
there to be read: without it `has_exchange_flags()` is false, the flags copied onto the records
read as "unknown", and nothing about them is tested at all. `UNIQUE::Hrimsorrow` is there for the opposite reason: it is what turns a Valdo
map's printed `Foil Hrimsorrow` into a name the trade site accepts. A **blighted** map needs no
record at all, which is itself the point — it resolves against `ITEM::Map` like every other one.

`UNIQUE::Hrimburn` is there to make `ITEM::Goathide Gloves` a base with **two** uniques on it,
which is the whole of what covers an unidentified unique: the gloves are the case only the user
can settle and the Riveted Boots above are the one the app takes for itself. The pair is also what
covers `en-items-base.index.bin` at all, since nothing else in the fixture reads it.

The **nine mod-pool entries** are chosen for the reader rather than for any item, and cover all
three domains. Five are the original set: a wording that prints no number (so no bounds at all,
which must not read as bounds that failed to parse), one wording shared by the map pool and the
chart pool under a trade id they agree on (which is the whole case for a domain-qualified index
key), a modifier printing two wordings of which only one carries a range, an entry whose wording
trade indexes under two hashes and which therefore carries no id at all, and a corruption implicit,
whose hash is in the implicit namespace.

The other four are the heist pool and are two wordings between them. `MapBurningGround` and
`HeistContractBurningGround` say the same thing and grant nothing else, so they fold into one row
across *different pools* — which is the whole of what a third domain had to be shown not to break.
`HeistContractBurningGround1` is that wording plus the two alert-level stats no contract prints, so
the fold leaves it alone, and `HeistContractMonsterPatrolAdditionalElite1` is that shape with
nothing to share: one printed wording, two unprinted, and the entry a printed line has to be
expanded to reach. Four of the nine entries' wordings are in `STATS` as well, so the pool-to-stat
join is covered; `Area contains many Totems` is there for that and for nothing else.

`tests/data/exchange/digest.json` is a slice of one real hourly digest, and every market in it is
there to be dropped or kept for a stated reason: the chaos/divine pair (the rate, read from both
sides), an Allflame ember whose ratio counts move on *both* sides (which is what proves the band
is ordered rather than named), an Awakener's Orb where they do not, a market against neither
denominator, one published all zeros, and one Hardcore Allflame row for the league filter.

`tests/data/ninja/` is the same idea for the reference price: real poe.ninja responses cut down to
the lines a case turns on, kept verbatim so a payload change reads back as a parse failure rather
than as a test that quietly stopped covering anything. Each one is there for a reason — the
currency market for the rate, `unique-armour.json` for a variant the item's own modifiers resolve,
`unique-accessory.json` for one they cannot, `skill-gem.json` for the tiers poe.ninja publishes
against the ones it does not, `base-type.json` for the two bases the captures already cover —
`item_6`'s Twilight Regalia (item level 84, eldritch influences that must be ignored) and
`item_7`'s Infiltrator Mitts (item level 78, under everything poe.ninja publishes) — and the two
map-item feeds, `fragment.json` for the exchange half (where the line's id, `phoenix`, is not its
page slug, `fragment-of-the-phoenix`) and `invitation.json` for the stash half.
