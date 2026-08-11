# Contributing

**This project is in early development and is not accepting pull requests.**

That is not a comment on anyone's code. The layers are still moving underneath each other - the
item, trade and reference-price layers are built but their seams are being redrawn as the
remaining pieces land - and reviewing patches against a design that changes weekly costs more than
it produces, for both sides. A pull request opened today would likely sit until it stopped
applying, which is a worse outcome than not opening it.

Pull requests will be closed unasked-for, unread. Please do not spend an evening on one.

## What is genuinely useful

**[Issues](https://github.com/JIRPOS/PathOfPriceCheck/issues).** They are read, and the following
are worth more than a patch would be right now:

- **A price check that got it wrong.** The item text (Ctrl+C in game, paste it in), what the tool
  showed, and what it should have shown. A wrong-but-confident price is the failure mode this
  whole design is organised against, so these matter more than crashes.
- **A modifier that was not searched**, or one searched on the wrong stat.
- **A build failure**, with your distribution and the exact CMake or compiler error. The
  per-distribution package lists in [BUILDING.md](BUILDING.md) are verified by hand for everything
  except Debian/Ubuntu, so a wrong package name there is a real bug in the docs.
- **The overlay landing in the wrong place**, with your resolution and whether the game is
  fullscreen, windowed or borderless.
- **The copy hotkey doing nothing.** Turn on Settings → Diagnostics → debug log, reproduce it, and
  quote the four-character check id from the panel footer (clicking it copies it). Read the log
  before attaching it - it contains whatever was on your clipboard. See [PRIVACY.md](PRIVACY.md).

## Forking

The code is MIT ([LICENSE](LICENSE)) and fork away - that is what it is for. Note that the fonts
and the game data have terms of their own; [ATTRIBUTION.md](ATTRIBUTION.md) says what they are, and
they follow the code into a fork.

## Later

When the design settles this file will say something different. Watch the repository if you want to
know when.
