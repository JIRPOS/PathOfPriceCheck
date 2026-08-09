// The gallery's enhancement layer, and every part of it is optional by construction: without
// this file the strip still swipes, scrolls, takes arrow keys and answers the numbered links,
// and a slide's image still opens as its own page. What the script adds is the three things a
// page that cannot know which slide is showing cannot do — prev/next arrows, a rotation, and a
// pop-out that keeps the reader on the page.
//
// `js` on <html> is set inline in the head, before first paint, so the stylesheet can hide the
// scrollbar without it appearing and disappearing — and, since hiding it shortens the strip, so
// the page does not shift under the reader on load.

(() => {
  const strip = document.querySelector(".shots .strip");
  if (!strip) return;
  const slides = [...strip.querySelectorAll(".slide")];
  if (slides.length < 2) return; // one screenshot needs neither arrows nor a rotation
  const dots = [...document.querySelectorAll(".dots a")];

  const still = window.matchMedia("(prefers-reduced-motion: reduce)");
  const scrollBehavior = () => (still.matches ? "auto" : "smooth");

  // Slides are `flex: 0 0 100%`, so the strip's own width is one slide.
  const current = () => Math.round(strip.scrollLeft / strip.clientWidth);
  const go = (i) => {
    const n = slides.length;
    strip.scrollTo({ left: (((i % n) + n) % n) * strip.clientWidth, behavior: scrollBehavior() });
  };

  // ---- arrows -------------------------------------------------------------------------------
  //
  // Wrapped here rather than emitted by the build script: the wrapper exists only to position
  // buttons that exist only when this runs.

  const wrap = document.createElement("div");
  wrap.className = "stripwrap";
  strip.parentNode.insertBefore(wrap, strip);
  wrap.appendChild(strip);

  const arrow = (dir, label, glyph) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = `arrow ${dir}`;
    b.setAttribute("aria-label", label);
    b.textContent = glyph;
    b.addEventListener("click", () => {
      stop();
      go(current() + (dir === "next" ? 1 : -1));
    });
    wrap.appendChild(b);
    return b;
  };
  arrow("prev", "Previous screenshot", "‹");
  arrow("next", "Next screenshot", "›");

  // ---- which slide is showing ---------------------------------------------------------------

  let shown = -1;
  let queued = false;
  const mark = () => {
    queued = false;
    const i = current();
    if (i === shown) return;
    shown = i;
    dots.forEach((d, n) => d.setAttribute("aria-current", n === i ? "true" : "false"));
  };
  strip.addEventListener(
    "scroll",
    () => {
      if (queued) return; // a scroll fires far oftener than a frame is drawn
      queued = true;
      requestAnimationFrame(mark);
    },
    { passive: true },
  );
  mark();

  // ---- the rotation -------------------------------------------------------------------------
  //
  // Hovering or tabbing into the strip pauses it, and so does the tab going to the background or
  // the gallery leaving the viewport — a rotation nobody is looking at is only work. Taking hold
  // of it at all stops it for good: once the reader is driving, moving the strip out from under
  // them is the failure this whole thing would be blamed for.

  const kPeriodMs = 5000;
  let timer = null;
  let stopped = still.matches; // a rotation is motion; asked not to, we do not start one

  // Named flags rather than a count: the reasons overlap and none of them pairs up reliably —
  // an observer's first callback can say "paused" as readily as "running", and a count would
  // then never come back down.
  const held = { hover: false, focus: false, hidden: false, offscreen: false };
  const paused = () => Object.values(held).some(Boolean);

  const halt = () => {
    if (timer !== null) clearTimeout(timer);
    timer = null;
  };
  const tick = () => {
    timer = null;
    if (stopped || paused()) return;
    go(current() + 1);
    timer = setTimeout(tick, kPeriodMs);
  };
  const start = () => {
    if (!stopped && !paused() && timer === null) timer = setTimeout(tick, kPeriodMs);
  };
  const hold = (what, on) => {
    held[what] = on;
    if (on) halt();
    else start();
  };
  function stop() {
    stopped = true;
    halt();
  }

  strip.addEventListener("mouseenter", () => hold("hover", true));
  strip.addEventListener("mouseleave", () => hold("hover", false));
  strip.addEventListener("focusin", () => hold("focus", true));
  strip.addEventListener("focusout", () => hold("focus", false));
  document.addEventListener("visibilitychange", () => hold("hidden", document.hidden));

  for (const ev of ["pointerdown", "wheel", "touchstart", "keydown"]) {
    strip.addEventListener(ev, stop, { passive: true });
  }
  dots.forEach((d) => d.addEventListener("click", stop));
  still.addEventListener("change", (e) => (e.matches ? stop() : start()));

  if ("IntersectionObserver" in window) {
    hold("offscreen", true); // until the observer says otherwise, a frame from now
    new IntersectionObserver(([e]) => hold("offscreen", !e.isIntersecting), {
      threshold: 0.25,
    }).observe(strip);
  }
  start();

  // ---- the pop-out --------------------------------------------------------------------------
  //
  // The link stays a link to the image, which is what a reader without this — or with a middle
  // click — gets. With `<dialog>` the click is taken over instead: Escape, the focus trap and
  // the return of focus to the link are the element's own, so what is left is the backdrop
  // click and a button for the readers who look for one.

  const box = document.createElement("dialog");
  if (typeof box.showModal !== "function") return;
  box.className = "lightbox";
  const close = document.createElement("button");
  close.type = "button";
  close.className = "close";
  close.setAttribute("aria-label", "Close");
  close.textContent = "×";
  const full = document.createElement("img");
  box.append(close, full);
  document.body.appendChild(box);

  close.addEventListener("click", () => box.close());
  // The dialog's own box is the image; anything else inside its rectangle is the backdrop.
  box.addEventListener("click", (e) => {
    if (e.target === box) box.close();
  });
  box.addEventListener("close", () => {
    full.removeAttribute("src");
  });

  for (const link of strip.querySelectorAll(".slide a")) {
    link.addEventListener("click", (e) => {
      if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey || e.button !== 0) return;
      e.preventDefault();
      stop();
      const img = link.querySelector("img");
      full.src = link.getAttribute("href");
      full.alt = img ? img.alt : "";
      box.showModal();
    });
  }
})();
