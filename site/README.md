# The website

`https://jirpos.github.io/PathOfPriceCheck` — built by [`scripts/build-site.sh`](../scripts/build-site.sh)
and deployed by [`.github/workflows/pages.yml`](../.github/workflows/pages.yml).

```sh
./scripts/build-site.sh            # -> _site/
python3 -m http.server -d _site    # http://localhost:8000
```

Only `index.html`, `template.html` and `style.css` are written by hand. **Every documentation
page is rendered from the .md file at the repository root** through GitHub's own `/markdown`
endpoint, so there is nothing here to keep in step with those documents and a doc page reads
exactly as the repository does. Adding one means adding a line to `PAGES` in the build script.

The download buttons are baked in at build time from the latest release, because our asset names
carry the version and there is therefore no fixed `releases/latest/download/...` URL to link. That
is why publishing a release redeploys the site.

Not deployed: this file.

## Screenshots

`./scripts/capture-screenshots.sh` regenerates every one of them, and the gallery publishes
**only the files that exist**, so a deleted capture drops out of the page rather than breaking it.
Each is a slide in a scroll-snap strip with its selling points beside it, written as
`file | headline | bullet ; bullet ; bullet` in the build script's `SHOTS` — no `|` or `;` in
the copy, they are the separators.

Nothing on the page is scripted, which is what decides how the strip is driven: swiping,
scrolling, arrow keys and the numbered links are all the browser's. Prev/next arrows are the one
thing that cannot work, since without JavaScript nothing on the page knows which slide is
showing, so the numbers jump directly instead. Every slide stays in the document either way, so
a search engine indexes all seven rather than the first.

The overlay is drawn on a **virtual X display**, which is what makes this reproducible at all:
nothing appears on screen, the system-wide hotkey grabs land on `:99` where they bother nobody,
and the screen is black everywhere the overlay did not paint, so trimming leaves the panel and
the item beside it and nothing else. `PPC_DEV_ITEM` opens the panel on a captured clipboard with
no game running; the one shot that cannot be staged that way — the seller's item drawn beside a
hovered listing — is taken by moving the pointer over a row with `XTestFakeMotionEvent`, since a
plain `XWarpPointer` moves the pointer without generating the motion a widget reacts to.

Every shot is cropped to a **common 980px height**, which is what the Settings dialog measures
and comfortably clears the tallest panel. That also crops away the footer, which on a machine
with the debug log on draws the check id and is not worth publishing.

The run does two things worth knowing. It **spends real trade API requests**, one search per
item, because a screenshot of an empty results table sells nothing. And it overrides
`auto_search` and `debug_log` for the duration and puts the config back afterwards — without
that the Settings shot shows this machine's state and its home directory rather than what a new
user sees.

Check each one before committing: a listing row carries the **seller's** handle (public on the
trade site anyway), and **your own** listings are tinted green with `(you)` whenever
`account_name` is filled in.
