# Reporting a bad price check

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

The **Report a bug** button on the price-check panel, the dialog it opens, and what that dialog
posts. The thing on the other end — the Cloudflare Worker, the Discord forum, and the injection
rules every field of the payload is held to — is [worker/README.md](../worker/README.md), and this
doc stops at the request.

Files: `src/report/` (pure, in `ppc_core`), `src/report_service.cpp` (the request),
`src/screens/report_screen.cpp` (the dialog), `src/capture.hpp` and `Overlay::request_capture`
(the screenshot), `src/util/png.cpp` (the encoder).

## The premise

**The dialog is the disclosure.** Everything the report will send is on screen, in the same text
that goes on the wire, before Send is pressed — not a summary of it, and not a promise about it.
That is the only reason it is defensible to offer a screenshot at all, and it is what every design
decision below is downstream of.

Two consequences that are easy to undo by accident:

- **The payload is frozen when the dialog opens** (`App::finish_bug_report`). The item text, the
  parse dump and the four version strings are copied into `ReportDraft` and never rebuilt. They
  could otherwise change under a reader: the data updater swaps a bundle in from its own thread,
  a search lands, the user picks a different unique. A preview that is one frame behind what will
  be sent is a preview that says nothing.
- **Nothing may be added to the request that the dialog does not draw.** `report::to_json` writes
  five keys and `tests/report_test.cpp` asserts the count, which is what that test is for.

## The button

`draw_action_bar` in `pricecheck_screen.cpp`. Three square glyph buttons against the panel's right
edge — right to left: Search, open on the site, report — and the last of them is on **every**
price check, including the items that get no search at all (currency, cards, scarabs), where it is
the only button and sits under whatever section drew last.

The glyphs are `ui/glyphs.hpp`, which is a contract with `scripts/fetch-glyphs.sh`: a name here
that is not a codepoint there bakes as nothing. `Fonts::has_glyphs` is the check, and every button
carries a single-letter fallback.

`icon_button` goes silent while `App::report_capture_pending()` is true — see below.

## The screenshot

`Overlay::request_capture()` sets a flag; `Overlay::end_frame` reads the back buffer with
`glReadPixels` **after `RenderDrawData` and before `SDL_GL_SwapWindow`** — what a swap leaves in
the back buffer is undefined.

### Two frames, and both of them are the point

```
press → [this frame finishes as it was] → masked frame, read back → dialog
```

`App::open_bug_report` does nothing but move `Opening::No → Masking`. The run loop advances it
between frames, which is where every part of this has to happen: the panel must be *redrawn* in
the face it will be photographed in, the read-back must be of a frame that is finished, and the
resize the dialog brings must not land inside one.

**The middle frame is the panel drawn to be photographed rather than to be read**, which
`App::report_capture_pending()` is the whole of. Two things follow from it, both in
`pricecheck_screen.cpp`:

- **Every seller's account name is replaced by its position** — `seller 1`, `seller 2` — including
  the user's own, on the row `account_name` marks as theirs. What a maintainer needs off the
  picture is that these are twenty different sellers and which row is which; the handles are
  somebody else's name and are worth nothing in a bug report. Prices, ages and the counts stay,
  because those are what a mispricing is read against. The predicate is `App::mask_sellers`, not
  `report_capture_pending` directly: `PPC_DEV_ANON` holds the same masking on for a whole run,
  which is what the website's screenshots are taken with, and both exist for the same reason.
- **No button shows its tooltip.** The cursor is on the report button when it is pressed, so
  without this every capture carries "Report a Bug" hovering over the panel — a picture of the act
  of reporting rather than of the thing being reported.

Masking on the press frame instead would work, today, only because the action bar happens to be
drawn before the results table. That is not a thing this file can promise, and the failure would be
silent and would consist of publishing somebody's name.

The masked frame is on screen for about sixteen milliseconds before the dialog covers it.

Two corrections on the way out, both in `Overlay::read_back`:

- **Rows are flipped.** GL's origin is the bottom-left and every consumer here wants the top-left.
- **Colour is un-premultiplied.** ImGui blends `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` onto a
  framebuffer cleared to transparent black, so what accumulates is colour already multiplied by
  its coverage — right for a compositor, and a shade too dark for anything reading the file as
  ordinary straight-alpha RGBA, which is every PNG viewer and Discord.

**It is our own window and cannot be anything else.** Nothing here can photograph another window;
the transparent parts of the overlay come back transparent, not as the game behind them. That is
the property `PRIVACY.md` states and the reason the feature exists in this shape rather than as an
OS screen capture.

It still contains whatever else the panel was showing, so **the checkbox starts unticked**, the
preview is large enough to read, and the dialog states in as many words both what the picture is
and what was taken out of it before it was taken.

## The PNG

`util/png.cpp`, zlib for the deflate. The smallest encoder that produces a file every decoder
accepts: one `IDAT`, no interlacing, and **no ancillary chunks at all** — no timestamp, no text,
no gamma. On a picture leaving a user's machine that is a property, not a shortcut.

Every row is filtered **Sub**, which stores each byte as its difference from the pixel four bytes
to its left. A panel is mostly flat horizontal runs, and Sub turns those into runs of zeros before
deflate sees them; a 900×1080 capture lands around 190 KB, against 3.9 MB raw and a relay cap of
5 MB. Per-row filter selection would do a little better and is not worth the code for one picture
a user sends by hand.

zlib is `ppc_core`'s only link dependency besides nlohmann/json. It was already in the tree on
Windows, where curl fetches it; elsewhere it is a system package that libcurl needs anyway.

## The parse dump

`report::describe` — the field this whole feature exists to deliver. Plain lines rather than JSON,
because the question it has to answer at a glance is *which line went wrong*, and a nested
structure answers that worse.

Sections: the item as parsed, what it resolved to in the bundle, the properties, **every modifier
with the stat record it matched**, the derived numbers, and the plan the search would have been
built from. The line that earns the rest is `-> NO MATCH`: a wording nothing in the bundle
recognises is the most common thing behind "this priced wrong", and a dump that quietly dropped
such a modifier would hide the exact bug it was sent to report.

`meta.bundle` is the version of the bundle **the item was resolved against** (`item_data_`), not
whichever is current — a mispricing is as often the data's as the code's, and the two can already
differ by the time the dialog opens.

## The request

`ReportService`, shaped like `LeagueService`: main thread touches every member, the worker owns
its stack and hands the result back through the SDL event queue.

- **Not through `trade::request`.** The shared rate limiter exists for GGG's policy and nothing
  else belongs behind it. What keeps this endpoint from being hammered is the relay's own per-IP
  cap and the fact that a report is a thing a person types.
- The screenshot reaches the worker as **raw pixels**. Encoding it, base64-ing it and serialising
  the body are together most of a second on a large panel, and none of that belongs in the frame
  that drew the button.
- 30 s timeout, against 8 s everywhere else: a report is megabytes on a slow line, and a send that
  gives up under a user watching it is worse than one that takes a while.
- `report::read_response` reports the **relay's own wording** for a refusal. It says why in every
  error body and that is a better message than any status-code table here could be. A 200 with no
  id is a failure — a proxy or a captive portal, not the relay — and reporting it as sent is the
  one lie this dialog must not tell.

## The two outcomes

- **Sent**: the dialog closes and `Screen::ReportSent` puts up a small confirmation with the id on
  it. That id names the forum post the report became, so it is the only handle either side has on
  a particular report.
- **Refused**: the dialog **stays open**, with a modal over it carrying the reason. The text the
  user wrote exists in that window and nowhere else; closing it on a failure would be losing the
  report. The modal reopens itself from `ReportState::Failed`, so dismissing it means *clearing*
  that state (`App::dismiss_report_result`), which also re-enables Send.

Neither screen dismisses on a click away, for the reason Settings does not and one of its own. Both
take the keyboard, since the dialog is mostly a box to type in.

## Testing it without posting anything

`PPC_REPORT_URL` overrides the endpoint. Point it at a local server that answers
`{"ok":true,"id":"deadbeef"}` to see the success path end to end — including what actually arrives
in the body — and at an unresolvable host to see the failure modal. `worker/send-test.sh` is the
other half: it posts the same shapes at the real relay, including a `--hostile` payload.
