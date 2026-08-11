# The road to 1.0

What is planned between here and a 1.0 release, in the order it is planned to arrive. A statement
of intent, not a contract: there are no dates, and anything here can be cut. What will not happen
is a feature shipping half-finished to make a number go up.

A version that has shipped stays on this page, marked **shipped** and written in the present tense,
so the list reads as the whole road rather than only the part still ahead.

**One feature per minor, no feature on a build.** `MAJOR.MINOR` is the [VERSION](VERSION) file and
`BUILD` is the CI run counter, so `0.6.31` → `0.6.32` is a fix and `0.6` → `0.7` is the next thing
on this list. A minor stays open as long as its fixes keep arriving. The release workflow owns that
file; it is never edited by hand.

**0.8 is the only version whose place is not fixed.** It waits on the
[data repository](https://github.com/JIRPOS/PathOfPriceCheck-Data) rather than on this one, and
lands whenever the first localised bundle is ready - if that is early, everything behind it shifts
up.

**There is no 0.5.** The release after 0.4 was cut with the minor already moved by hand, so the
bump landed twice and QuickPaste shipped as 0.6. The number is spent; nothing is missing from this
page.

For what is deliberately *not* planned, and why, see [docs/roadmap.md](docs/roadmap.md).

## 0.3 - The application updates itself - **shipped**

The executable. The game data had updated itself from the start; the program had not. On Windows
that also meant shipping an installer, because where the tool lives decides whether it can.

**Does:**

- Check this repository's releases when it starts and again while you play, downloading and
  verifying in the background.
- Apply the update **as the tool closes**, so the next start is already the new version - never
  to a running program.
- Tell you in three places: the idle status marker, the price-check panel, and a Settings row
  with **Restart now**.
- **Never restart itself unasked.** **Restart now** is you asking, and only then does it close and
  come back. Dismiss the notice and the update waits for whenever you next close the tool.
  Nothing closes while the game is running.
- Update the AppImage in place, at its own path, so you do not end up with two launcher entries.
- Offer, but not apply, an update it cannot install here - a Windows zip unpacked into
  `Program Files`, a copy your package manager owns, a release with no download for your platform.
  The notice says which of those it is and points at the release page.
- Have a setting to turn it off, defaulting on.

**And Windows got an installer**, because the two are the same problem. Unpack a zip into
`Program Files` and the tool cannot update itself there; a per-user install goes to
`%LOCALAPPDATA%\Programs`, needs no administrator, and is writable by definition. It brings a
start-menu shortcut, an optional desktop one and a proper uninstaller - and it is where any
prerequisite would ever go, if one is ever needed. The portable `.zip` stays for anyone who wants
a folder they can move.

One new request and two new files, all listed in [PRIVACY.md](PRIVACY.md).

## 0.4 - The filter list becomes yours to change - **shipped**

The plan seeded each modifier's filter from the roll and the **Range matching** setting, and you
could only tick it or leave it.

**Does:**

- Let you set the bounds on any row that carries a number - a slider with both ends on one track,
  or the two numbers typed. Both, a floor, a ceiling, or neither.
- Keep the modifier's own roll range on screen beside what you asked for, and let you ask for more
  than it: the slider does not stop at what your copy rolled.
- Give each row a reset back to the value the tool seeded.
- Put the filters a search deliberately leaves out - a map's affixes, a beast's monster modifiers -
  under a section at the foot of the list, so a question the tool does not ask by default is still
  one you can ask.
- Search a linked item on its sockets and its links. Six sockets and a six-link were no part of the
  price before, which is most of what such an item is worth.
- **Not search on an edit.** The Search button sends, as it always has.

**Does not** - carry an edit onto the next item. Bounds belong to the item in hand.

## 0.6 - QuickPaste - **shipped**

A hotkey opens a small window at the cursor listing saved snippets - a map regex, a vendor
search, a whisper you send twenty times an evening.

**Does:**

- Hold entries of a heading and a body, multi-line, written and arranged in Settings' own
  **QuickPaste** tab: add, edit, delete, and drag into the order you want them offered in.
- Put the one you pick on the clipboard and give the game back the foreground.
- **Pick by number key**, so the mouse never has to travel - and by the key's *position*, so it
  works on a keyboard layout that does not print digits on that row.
- Offer nine at a time, which is how many number keys there are. Keep as many as you like: the
  ones beyond nine are simply switched off until you switch something else off. Nothing to read,
  nothing refused - the tenth tick is just not available.
- Open at your cursor, growing down, up, or from somewhere between the two, whichever fits the
  screen.

**Does not** - press Ctrl+V for you. You paste, in your own field, at your own time.

**Might** - grow that keystroke as an option later if it turns out to be wanted.

## 0.7 - Map check

A hotkey that reads a map's rolled modifiers and tells you which ones you decided you cannot take.

**Will:**

- Mark each modifier **safe**, **dangerous** or **deadly**, and remember it.
- Lead with the worst verdict on the map.
- Draw unrated modifiers as unrated, with the rating control on the spot. The table fills in by
  being used.
- Keep a table per character profile, picked in the popup and sticky until you change it. A new
  profile can start as a copy of an existing one.
- Rate a modifier as a modifier, at every roll - the way a map regex does. **Will not** ask you
  for a threshold on each of a few hundred mods.

**Might - seed the table from a map regex you already use.** Paste
`"!\d+ e|te of|m resistances$|ents$|r, f|ter e|ll damage$|from$|t reg|s def|h tem" pte` and every
modifier an excluding term hits is *proposed* dangerous, every one a wanted term hits *proposed*
safe. You confirm; from then on the table is what the tool believes. It imports imperfectly on
purpose - the game's search reads a whole item where this reads modifier wordings, and a term
written against a printed number is being matched against a placeholder - so it is a head start,
not an answer. The regexes come from the paste list in 0.6.

**Might - switch profile automatically** by watching `LatestClient.log`. Outside the 1.0 promise:
it ships if it is cheap and is dropped without argument if it is not. Either way it goes in
[PRIVACY.md](PRIVACY.md), as does the verdict table.

## 0.8 - Every language the client speaks

Already built on this side: every word the client prints is read from a table, and Settings has a
**Client language** row. Only the data is missing, which is why this version floats.

**Will:**

- Read, price and draw an item from a client in any language the bundle carries, using the
  fallback font your system already ships for the scripts the bundled typeface lacks.
- Ship each language only once it has been checked against a real capture from a client actually
  set to it.

## 0.9 - This tool's own text, in more than English

Reading a translated client and translating the buttons are two different problems and two
settings; the machinery for the second is written and only the tables are missing.

**Might** - Latin-script languages first. **This is the one version here that can be dropped**:
it is cosmetic, 1.0 does not wait on it, and if it slips past 1.0 the README will say so.

## The long window - the last minor before 1.0

**No features. Fixes only, for as long as it takes**, and longer than the windows between the
versions above.

1.0 goes out when:

- No known case produces a **wrong number**.
- Windows and Linux X11 are both exercised on real hardware against the live game.
- Every language the bundle declares has been checked against a capture from a client set to it.
- The updater has carried at least two releases end to end, including one on the AppImage.
- [PRIVACY.md](PRIVACY.md) matches the code - every host, every file.

## After 1.0

Nothing promised. Two that get asked about:

- **Click-through on the overlay.** A real gap, and the reason the window is never larger than it
  has to be.
- **A native Wayland backend is decided against, not delayed.** Proton runs the game as an
  Xwayland client, so going native would trade capability away for nothing. Revisited only if
  that changes.
