<div align="center">

# ✦ SuperMod ✦

### A native productivity super-suite for your reMarkable tablet

**Cross-document linking · a full perpetual Planner app · native-ink notes everywhere · bookmarks · a knowledge graph — all rendered by xochitl itself, all backed by a real SQLite database.**

<!-- TODO(screenshots): replace with the hero shot — the Planner month view over a notebook -->
<img src="docs/screenshots/hero.png" alt="SuperMod hero" width="720"/>

> ⚠️ **Supported devices: reMarkable 1 & reMarkable 2 only** (for now). reMarkable Paper Pro is not supported yet.

</div>

---

## What is SuperMod?

SuperMod is a bundle of QML mods for the reMarkable's `xochitl` UI, glued to a single C++/SQLite backend. It does **not** replace xochitl or run a separate app — every screen you see is the *real* xochitl UI, patched at runtime with [`qmldiff`](https://github.com/asivery/rm-xovi-extensions) and driven by a native extension loaded through [`xovi`](https://github.com/asivery/rm-xovi-extensions).

The whole suite follows one rule — the **BLoC line**:

- **QML owns only the view** — layout, local view-state, and user-intent handlers that call *one* backend method.
- **C++ + SQLite own all the logic** — persistence, queries, FTS5 search, transactions, validation, ordering, dedup, cross-document relations, reminders, threading.

State flows **down** as commands (`Q_INVOKABLE`) and **up** as change signals; the UI re-pulls a fresh snapshot and never computes next-state from prev-state. The result is fast, crash-resistant, and survives reboots.

---

## ✨ Features

> 📸 The screenshots below are placeholders — the full gallery is captured in `docs/screenshots/` (see [the manifest](docs/screenshots/README.md)).

### 🗓️ Planner — a full perpetual calendar & planner app
The headline feature. A dedicated, full-screen planner launched from the My-Files sidebar **and** from the Quick-Settings panel (next to Airplane mode).

<img src="docs/screenshots/planner-month.png" alt="Planner month view" width="640"/>

- **Year / Month / Week / Day** views, pixel-faithful to a high-contrast paper planner, fully **perpetual** (real dates in SQLite, any year).
- **Events & tasks** — tap a time slot to add an event; checklists for Focus / Priorities / To-do, with reorder.
- **Native ink everywhere** — write by hand directly on the calendar: a per-half-hour handwriting strip in the Day view, sketch pages, day notes, week & month/year reviews, and draggable **pen chips** you can drop on any page. All real xochitl ink (pen, eraser, lasso, paste), shown inline.
- **Notification center** — a bell with an unread badge; reminders fire **30 / 15 / 5 minutes** before an event through the **native** reMarkable notification banner (works with the planner open *or* closed). A persistent history grouped by day (Today / Tomorrow / date), with **mark-read**, **clear**, and **snooze** (10 / 30 / 60 min).
- **Habits** tracker, yearly **Goals**, a **Mood / pixel** tracker, themed **Collections**, and a monthly **Budget** with a custom numeric keypad.
- **Data dashboard** — counts for every table and a guarded bulk reset.
- Internationalized (labels via `qsTr`, month/day names follow the device language).

### 🔗 Chiplinks — cross-document links & backlinks
<img src="docs/screenshots/links.png" alt="Cross-document links" width="640"/>

Create hyperlinks between any two documents/pages. Every link is bidirectional — each target shows its **backlinks** through a SQL `VIEW`, so you can navigate your notes like a wiki.

### 🕸️ Graph view
<img src="docs/screenshots/graph.png" alt="Knowledge graph" width="640"/>

A force-directed graph of your documents and the links between them — see the shape of your knowledge base at a glance.

### 📑 Table of Contents
<img src="docs/screenshots/toc.png" alt="Table of contents" width="640"/>

A live, navigable TOC for notebooks — native headings plus your own custom entries, with a section breadcrumb on the page.

### 📌 Pins & 🔖 Bookmarks
Pin pages for quick return and bookmark entries over the document's `tocItems` — pure, fast transforms over an already-loaded snapshot.

### 🖊️ Post-it scratch notes & 📷 Capture
<img src="docs/screenshots/postit.png" alt="Post-it scratch notes" width="640"/>

Drop a native-ink **post-it** anywhere — a tiny pointer to a hidden scratch page where xochitl persists your handwriting. **Capture** an area of the screen as an image and paste it straight into a note.

### 🧰 Toolbar & UI tweaks
Quality-of-life additions to the document toolbar and surrounding UI, all native-looking.

---

## 🏗️ Architecture

```
┌─────────────────────────────┐        commands (Q_INVOKABLE)        ┌──────────────────────────┐
│  xochitl QML UI              │  ───────────────────────────────▶   │  ChiplinksBackend (C++)  │
│  (patched at runtime via     │                                      │  + repositories          │
│   qmldiff .qmd modules)      │  ◀───────────────────────────────   │  + SQLite (WAL, FTS5)    │
│  view + local state only     │        *Changed signals → re-pull    │  all business logic      │
└─────────────────────────────┘                                      └──────────────────────────┘
```

- `qmd/` — the QML mods, grouped into packs (`supermod_planner`, `supermod_links`, `supermod_toc`, …). Authored as `qmldiff` `.qmd` files. The published `.qmd` are **hashed** (via [`rm-qmd-hasher`](https://github.com/rmitchellscott/rm-qmd-hasher) against reMarkable OS `3.27.1.0`) so they don't expose xochitl's internal QML identifiers, and code comments are stripped for the same reason.
- `chiplinks-backend/` — the C++ extension: `ChiplinksBackend` (the `Q_INVOKABLE` + signal surface), per-feature repositories, the schema migration ladder, and the SQLite vendor sources.

---

## 🔨 Building

The extension builds in Docker (no host toolchain needed):

```bash
bash chiplinks-backend/build.sh
```

This produces `chiplinks-backend/chiplinks-backend.so` — the xovi extension that registers the backend **and** applies every `.qmd` patch at load time.

> A green build proves the code compiles — it does **not** prove the `qmldiff` patches apply. `qmldiff` runs at *runtime*; the ground truth is the device log (no `Error while processing`, an `Loaded external …` line per module).

---

## 📦 Dependencies

SuperMod is a `xovi` extension and relies on the xovi runtime plus two companion extensions on the device:

- **[xovi](https://github.com/asivery/xovi)** — the extension loader/runtime that makes all of this possible.
- **[qt-resource-rebuilder](https://github.com/asivery/rm-xovi-extensions)** — applies SuperMod's `qmldiff` QML patches to xochitl at load time.
- **[framebuffer-spy](https://github.com/asivery/rm-xovi-extensions)** — **required** (declared `depends-on` in `chiplinks-backend.xovi`): gives the Capture feature access to the framebuffer. Without it, xovi **silently skips loading all of SuperMod** — so it is not optional.

The simplest way to fetch the companion extensions is [vellum](https://github.com/vellum-dev/vellum-cli): `vellum add framebuffer-spy` pulls `xovi` + `qt-resource-rebuilder` + `framebuffer-spy` and drops them into `~/xovi/extensions.d/`.

## 📲 Installing (reMarkable 1 / 2)

1. Install `xovi`, `qt-resource-rebuilder` and `framebuffer-spy` (e.g. with `vellum add framebuffer-spy`).
2. Copy `chiplinks-backend.so` into `~/xovi/extensions.d/`.
3. Load xovi **manually** and launch xochitl under it.

> 🛡️ **Safety:** load xovi manually — never wire it into the boot sequence. It is your anti-brick safety net: if anything goes wrong, just reboot and xochitl starts clean.

---

## 🙏 Credits & prior art

SuperMod stands on the shoulders of the reMarkable hacking community:

- **[Asivery](https://github.com/asivery)** — `xovi`, `qmldiff`, and `qt-resource-rebuilder`, the runtime that makes all of this possible.
- **[rmitchellscott](https://github.com/rmitchellscott/rm-qmd-hasher)** — `rm-qmd-hasher`, used to hash the published `.qmd` so they never reveal reMarkable's internal QML structure.
- The emulator/RE lineage — **timower**, **Eeems**, **Jayy001**, and others.

This project is an independent, unofficial modification. **reMarkable** is a trademark of reMarkable AS; this project is not affiliated with or endorsed by them.

---

## ⚖️ Disclaimer

Modifying your tablet is at your own risk. SuperMod loads manually through xovi and never touches the boot configuration, but you are responsible for what you run on your device. No warranty.
