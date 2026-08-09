# External APIs — the load-bearing domain knowledge

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

We are **not** registering the application, so only **publicly accessible** endpoints are usable, and
we must send a descriptive `User-Agent` identifying the tool + contact, per GGG policy.

## PoE trade API (two-step, both rate-limited)

1. `POST https://www.pathofexile.com/api/trade/search/<league>` with the query JSON → returns a
   search `id` and a list of result hashes.
2. `GET https://www.pathofexile.com/api/trade/fetch/<comma-separated-ids>?query=<id>` — **fetch in
   batches of at most 10 ids** → returns listing + price detail.

Mods are not sent as text; they are **stat hashes** (e.g. `explicit.stat_1509134228`). The mapping
comes from static-data endpoints that must be fetched and cached:
`.../api/trade/data/stats`, `.../api/trade/data/items`, `.../api/trade/data/leagues`.

The site's map categories are **finer than the item class can be**: `data/filters` publishes
`map.fragment`, `map.scarab`, `map.invitation` and `map.breachstone` as separate options, while
`item-classes.ndjson` maps both "Map Fragments" and "Misc Map Items" onto `map.fragment` — which it
has to, because the clipboard's item class cannot tell a scarab from an invitation. It costs
nothing today (none of them are searched), but a real trade search for invitations needs the split,
and that is a data-repo job or an app-side keyword table like `ninja::map_item_type`.

The **gem** categories are finer too — `gem.activegem`, `gem.supportgem` and `gem.supportgemplus`
against the two classes the clipboard prints — and here it demonstrably does not matter, so do not
add a table for it: an Awakened support gem, which is `gem.supportgemplus` on the site and
`gem.supportgem` in the bundle, returns the same 57 matches under either and under the bare `gem`.
A gem is pinned by its `type`, and the category only ever narrows what that already decided.
The **filters** the gem search needs are all in `misc_filters`: `gem_level`, `quality`, and the
booleans `gem_transfigured` / `gem_vaal` / `gem_imbued`. The two flags are not used and could not
stand in for the discriminator anyway — measured, `type: "Raise Zombie"` with
`gem_transfigured: true` is **0 matches against 365** for the same type with `alt_y`, because a
bare gem type matches only the unaltered skill. There is no `gem_alternate_quality` any more.

## The in-game currency exchange (public, no OAuth)

`GET https://web.poecdn.com/api/currency-exchange[/<realm>][/<id>]`, documented at
<https://www.pathofexile.com/developer/docs/reference>. **Public** — no OAuth, no scope, no
registered application — and on the CDN, so no `X-Rate-Limit` headers and no per-policy budget.
The realm segment defaults to PoE 1 PC, which is what this binary drives.

`<id>` is the **unix timestamp of an hour**, and any hour can be addressed directly — walking from
`next_change_id` is not required. Omit it and you get the *first* hour of history (1722027600,
Settlers launch), which is never what you want. Each digest is ~2MB of every market in every
league: `{league, market_pair: [metadata id, metadata id], volume_traded, lowest_stock,
highest_stock, lowest_ratio, highest_ratio}`, all four maps keyed by the pair's metadata ids.
**No names anywhere** — see `src/exchange/` for what that costs and how it is paid.

## poe.ninja

Docs: <https://poe.ninja/docs/api>. **Only the economy endpoints are public**; the builds and
profile endpoints are closed to third parties and must not be touched. The old
`poe.ninja/api/data/currencyoverview` and `itemoverview` paths are **gone** (404) — PoE 1 is under
`poe.ninja/poe1/api/economy/`, and there are two overview endpoints with different payload shapes:

- `GET .../poe1/api/economy/exchange/current/overview?league=<league>&type=<Currency|Fragment|DivinationCard|Essence|Scarab|…>`
  — the currency market. `lines[]` is `{id, primaryValue (chaos), sparkline}`, joined by `id` to a
  sibling `items[]` for the name and icon; `core.rates.divine` is the chaos↔divine rate.
- `GET .../poe1/api/economy/stash/current/item/overview?league=<league>&type=<UniqueWeapon|SkillGem|…>`
  — what individual items are listed at: `{name, baseType, variant, chaosValue, divineValue,
  links, gemLevel, gemQuality, corrupted, detailsId, sparkLine, explicitModifiers}`.

Item pages are `poe.ninja/poe1/economy/<league-slug>/<category-slug>/<detailsId>`, and the league
slug is not the league id (see `league_slug`). There is no versioning and no SLA: the docs say
outright that breaking changes to these can happen without notice, which is why `parse_overview`
treats every field as optional and an unreadable payload as "no price" rather than an error.

Cache aggressively — 30 minutes, matching what poe.ninja sets on its own responses — send
conditional requests, and identify the app in the User-Agent. See `src/ninja/` above for how.

## Rate limits — treat as a hard requirement

GGG returns rate-limit state in response **headers** (`X-Rate-Limit-Rules`, per-policy
`X-Rate-Limit-<policy>` giving `hits:period:window` triplets, `X-Rate-Limit-<policy>-State`, and
`Retry-After` on 429). All GGG traffic passes through the shared rate limiter
(`trade/ratelimit`, owned by `trade/client`), which parses these headers, tracks each active window,
and **proactively delays** rather than reactively eating 429s. Never issue a GGG request outside
`trade::request`.
