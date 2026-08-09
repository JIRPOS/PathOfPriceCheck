# The website

`https://jirpos.github.io/PathOfPriceCheck` — built by [`scripts/build-site.sh`](../scripts/build-site.sh)
and deployed by [`.github/workflows/pages.yml`](../.github/workflows/pages.yml).

```sh
./scripts/build-site.sh            # -> _site/
python3 -m http.server -d _site    # http://localhost:8000
```

Only `index.html`, `template.html`, `style.css` and `gallery.js` are written by hand. **Every
documentation page is rendered from the .md file at the repository root** through GitHub's own
`/markdown` endpoint, so there is nothing here to keep in step with those documents and a doc
page reads exactly as the repository does. Adding one means adding a line to `PAGES` in the
build script.

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

**The strip is the browser's and `gallery.js` is an enhancement of it**, which is the rule the
whole gallery is built to: swiping, scrolling, arrow keys and the numbered links come from the
browser, every slide stays in the document either way — so a search engine indexes all seven
rather than the first — and each slide's image is a plain link to itself. What the script adds
is the three things markup cannot do, because none of them can be done without knowing which
slide is showing:

- **Prev/next arrows**, which wrap around.
- **A rotation**, one slide every 5s. Hovering or tabbing into the strip pauses it, and so does
  the tab going to the background or the gallery scrolling out of view; taking hold of it —
  an arrow, a dot, a swipe, a wheel, a key — stops it for good, because moving the strip out
  from under a reader who is driving it is the failure this would be blamed for. It never
  starts at all under `prefers-reduced-motion`.
- **A pop-out** for the image, so a click no longer leaves the page for a bare `.png` the
  reader has to come back from. It is a `<dialog>`, so Escape, the focus trap and handing
  focus back to the link are the element's own; the backdrop click and the close button are
  ours. A modified click, a middle click and a script that did not run all still get the link.

The scrollbar is hidden **only** when the script ran, since without the arrows it is the one
thing on the strip saying it scrolls. `js` on `<html>` is what the stylesheet tells the two
apart by, and it is set by an inline script in the head rather than by `gallery.js` itself:
hiding the scrollbar shortens the strip, so deciding it after first paint is a scrollbar
flashing and the page shifting under the reader.

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
