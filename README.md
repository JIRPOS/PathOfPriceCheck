# PathOfPriceCheck

Native, lightweight Path of Exile price-check overlay (C++20 / Dear ImGui). No Electron, no wrappers.

Hover an item in-game, hit the copy hotkey, get prices from the official trade site and poe.ninja in
an on-screen overlay.

See [CLAUDE.md](CLAUDE.md) for architecture and the decisions behind the stack.

## Versioning

`MAJOR.MINOR.BUILD` — `MAJOR.MINOR` lives in [VERSION](VERSION) and is bumped by the **Version bump**
workflow; `BUILD` is the cumulative CI run counter. Pushes to `master` publish a release for win64 and
linux.
