# Developer notes

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

The reasoning behind the code, one file per layer. [../CLAUDE.md](../CLAUDE.md) is the map and holds
the rules that apply everywhere; these hold the detail, and are meant to be read one at a time by
whoever — or whatever — is about to change that layer. New detail belongs here, not there.

| Doc | What it owns |
| --- | --- |
| [platform.md](platform.md) | The per-OS seams: hotkeys, foreground detection, input injection, clipboard, single instance. |
| [architecture.md](architecture.md) | `App` and the SDL loop, the copy path, focus, overlay placement, Settings, fonts, the debug log. |
| [data-layer.md](data-layer.md) | The runtime data bundle, the updater, the lexicon, stat normalization and matching. |
| [item-layer.md](item-layer.md) | Parse → resolve → derive → search plan, and the rules every strategy shares. |
| [strategy-unique.md](strategy-unique.md) | Uniques: the per-unique modifier join, and an unidentified copy. |
| [strategy-map.md](strategy-map.md) | Maps, charts and Valdo maps — the strategy that searches no affix. |
| [strategy-gem.md](strategy-gem.md) | Gems: the shortest search here, and all of it numeric. |
| [trade-layer.md](trade-layer.md) | The trade query, the client, the rate limiter, the results and filter UI. |
| [ninja.md](ninja.md) | The poe.ninja reference price. |
| [exchange.md](exchange.md) | GGG's hourly in-game currency exchange digests. |
| [localisation.md](localisation.md) | Reading a translated client, versus translating our own text. |
| [external-apis.md](external-apis.md) | The endpoints, their shapes and their rate limits. |
| [conventions.md](conventions.md) | Comment style, the maintainer alias, which docs are public. |
| [testing.md](testing.md) | Build prerequisites, sanitizers, fixtures and the bundle slice. |
| [roadmap.md](roadmap.md) | Constraints on the planned versions in [../ROADMAP.md](../ROADMAP.md), and what is deliberately not built. |
