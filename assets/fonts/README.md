# Fonts

The overlay renders in **Fontin** — the typeface Path of Exile itself uses — by Jos Buivenga
(exljbris). Homepage: <https://www.exljbris.com/fontin.html>

Four faces are **embedded in the executable** as base85-encoded, stb_compress'd blobs in
[`src/fontin_data.inc`](../../src/fontin_data.inc): Regular, Bold, Italic, SmallCaps. There is no
runtime asset dependency — the `.ttf` files here are only inputs for regenerating that blob, and
are not committed.

`Fontin-SmallCaps.ttf` is a separate family, not an OpenType `smcp` feature — which is what makes
small caps usable at all here, since ImGui does no text shaping or feature substitution.

We use the **TrueType** release, not the OpenType one: ImGui rasterizes with stb_truetype, whose
TrueType path is far better tested than its CFF/OTF path. Proper OTF handling would mean adding the
FreeType backend as a dependency.

## License

From the `ReadMe.txt` in the upstream archive (fetched to `Fontin-LICENSE.txt`):

> * This font is free for personal and commercial use.
> * This font may not be modified.
> * This font may not be distributed, online or on any media, without permission from Jos Buivenga.
> * This font may not be sold.
> * This font is the intellectual property of Jos Buivenga.

Bundling into a redistributed binary is distribution, so it is nominally covered by that third
clause. This is a deliberate, maintainer-level decision: the embedded form isn't a usable font file
to anyone who extracts it, the typeface is ~20 years old, and permission can be sought later. If it
can't be reached, swapping the typeface means regenerating one file — see below — with no code
change. Users can already override at runtime via `$PPC_FONT_DIR`.

## Regenerating

```sh
./scripts/fetch-fonts.sh     # downloads the TTFs here (gitignored)
./scripts/gen-font-data.sh   # rewrites src/fontin_data.inc
```

`gen-font-data.sh` calls `fetch-fonts.sh` itself if the TTFs are missing, and needs a configured
build tree for ImGui's `binary_to_compressed_c.cpp` (or `IMGUI_SOURCE_DIR` pointing at one).

## Runtime override

Set `PPC_FONT_DIR` to a directory containing `Fontin-{Regular,Bold,Italic,SmallCaps}.ttf` (or any
four TTFs under those names) and they replace the embedded faces. Missing non-Regular faces fall
back to Regular; a directory without `Fontin-Regular.ttf` is ignored with a log line.
