# Build "Magpie Memo" — a standalone GW2 notepad addon with chat-link chips

## What this is

A new, standalone Raidcore Nexus addon (Windows DLL, cross-compiled from Linux with MinGW), built
FROM SCRATCH in its own new directory. Magpie Memo is a notepad: multiple notes, multi-line
markdown content, and — its signature feature — GW2 chat-link "chips" embedded inline in the text
(waypoints, items, skins, skills, builds), rendered with icon + name + tooltip, resolved via the
Decoder Ring addon.

It is a UI addon (it owns a window and draws). It does NOT do any game-memory reading and does NOT
hook any game functions — it is pure UI + data. (Earlier design deliberately cut the
"click-waypoint-to-open-map" feature precisely to avoid that dependency.)

## Reference addons (all readable on this machine — read them, don't guess)

- **Alter Ego** — the SKELETON + THEME donor. Mirror Alter Ego's Nexus addon structure (dllmain /
  lifecycle / window registration / settings persistence / build setup / CMake MinGW cross-compile
  + host-test target). Critically: Magpie Memo MUST use the **same ImGui theme as Alter Ego,
  implemented the same way** (reuse Alter Ego's theme mechanism, don't invent a new visual style) so
  Magpie sits visually inside the PieOrCake suite. Read Alter Ego's actual theme code and replicate
  its approach.
- **Pie UI `src/chat/`** — the canonical source of the codec to VENDOR and the UI pieces to ADAPT
  (details below). Read these files directly.
- **Decoder Ring** — the resolution service Magpie consumes. Read its `docs/API.md` and
  `public/DecoderRingApi.h` on master directly (do NOT integrate from memory or this prompt's
  paraphrase — read the real header/contract).

## Vendored vs adapted — be precise, this distinction matters

**VENDOR (byte-identical copy, no-fork discipline, `#pragma once` intact):**
- `ChatLinks.{h,cpp}` + `SpecData.h` from Pie UI's canonical `src/chat/`. These are the pure
  structural codec. Copy them in unchanged; Pie UI's `src/chat/` is canonical upstream; never
  fork-edit (fix upstream then re-vendor). They give Magpie offline structural decode (type + ids +
  spans) and build/spec labels with no dependency on Decoder Ring.

**ADAPT (copy then modify — these are NOT used as-is):**
- `ChipTextEdit` (from Pie UI) — the editable text buffer where each chat link is one atomic cell
  (vanilla backspace/selection feel). Adapt it for Magpie: (a) make it **multi-line** (Pie UI's is
  a single-line chat input; a note is multi-line), and (b) **decouple it from Pie UI's
  `RichLineResolveChip` / `RichLine`** and repoint it at Magpie's OWN chip representation. A naive
  copy would drag `RichLine` (which is bound to Pie UI's texture cache and `FrApiHook`) along —
  do NOT bring `RichLine`/`RichLineResolveChip` across; Magpie draws its own chips.
- The icon URL→texture helper (reference: Pie UI's `SkillIconCache::RequestUrl`) — adapt into a
  cleanly-separable Magpie utility that downloads an icon URL and uploads it to a Nexus texture,
  cached, async, with the same "never poison-cache a failed download" discipline. Keep it as its own
  unit (it's the consumer-side counterpart to Decoder Ring; conceptually reusable).

**Do NOT vendor or copy:** `RichLine`, `RichLineResolveChip`, `FrApiHook`, or any game-memory /
game-hook code. Magpie has none of that.

## The v1 feature spec (this is the agreed scope — build exactly this)

### Layout — two-pane master/detail
- Left pane: list of note TITLES, with a **"Create New Note"** button at the bottom.
- Right pane: the selected note's content.

### Note model
- A note is fundamentally a **plain UTF-8 string**: markdown text with chat codes embedded inline
  as their raw chat-code text. Chips and formatting are PURE RENDER-TIME interpretation of that
  string — there is no structured document model, no per-run style storage. (This keeps notes
  trivially serialisable and makes future share-codes easy — though share-codes are v2, NOT now.)
- Each note also has a **title** shown in the left pane. Use a **separate explicit title field**
  (not first-line-derived) — deriving a clean title from a line that may contain chips/markdown is
  fuzzy; an explicit title field is unambiguous. New notes get a sensible default title (e.g.
  "Untitled note") that the user can edit.
- Persist notes to disk via the same settings/persistence approach Alter Ego uses. Notes survive
  restart.

### View / Edit modes
- Two modes for the right pane: **view** (rendered) and **edit** (raw source).
- **Embrace edit=source, view=preview.** Edit mode shows the raw markdown + chat codes as text
  (chips still render as atomic cells in the editor, but markdown is shown as raw syntax). View mode
  shows the rendered result.
- The two modes are visually similar EXCEPT edit mode has a **different background tint** so the
  user always knows they're editing.
- Creating a new note opens directly in **edit** mode.
- In edit mode, two buttons appear at the top: **Save** and **Cancel**. Both return to view mode;
  Cancel discards changes, Save persists them.
- **Dirty-state guard:** if the user has unsaved edits and selects another note in the left pane, a
  modal appears ("This note has unsaved changes" — Save / Discard / Cancel-the-navigation). Don't
  silently lose edits.

### Markdown (rendered in VIEW mode; shown as raw syntax in EDIT mode)
Support a SPECIFIC subset — standard markdown only:
- **Bold** (`**text**`) — rendered with a bold font variant (mid-line).
- *Italic* (`*text*`) — rendered with an italic font variant (mid-line).
- **Headings** (`#`, `##`, ... any level) — ALL levels render in `fontbig` FLAT (no per-level
  scaling; you only have one heading font). Store the level as-typed (don't rewrite the user's
  source), just map every level to fontbig at render time. A heading is a whole-line property (no
  mid-line font switch).
- **Bullet lists** (`- item`) — rendered as a list with appropriate indentation/markers.
- **NO underline** (not standard markdown).
- **NO format/context menu in v1** — the user types the markdown syntax themselves. (A smart
  right-click format menu is explicitly deferred to v2.)
- Use Alter Ego's / Nexus's available font handles for bold/italic/fontbig — read what's actually
  available and map to it. Nexus rendered text is ASCII-only; keep all rendered UI text ASCII.

### Chips (the signature feature) — rich display
Each embedded chat code renders as a chip with the SAME rich quality as Pie UI's chatbox:
- **Icon** — from Decoder Ring's resolved icon URL, downloaded + uploaded to a Nexus texture via
  Magpie's adapted icon helper, cached. While the texture is loading or if it fails, the chip falls
  back to text-only (name/structural label) — it must ALWAYS render and stay functional.
- **Name** — Decoder Ring's resolved display name. If Decoder Ring hasn't resolved it yet
  (not-ready), or is absent, show the **structural label** from the vendored codec (e.g.
  `[Waypoint]`, `[Item]`, and real spec labels like `[Mirage Build]` which come from the vendored
  `SpecData.h` with no service needed). Upgrade to the resolved name when the event arrives.
- **Tooltip** on hover — built from Decoder Ring's metadata record (name, and type-specific extras
  like item value / skill description-facts / waypoint map). Pure ImGui tooltip; Magpie owns it.
- **Right-click context menu** — two items for v1:
  - **"Copy chat code"** — copies the raw chat code (already stored in the note) to the clipboard.
    Works off the stored code; needs NO Decoder Ring.
  - **"Open in wiki"** — opens the GW2 wiki via its chat-code SEARCH url
    (`https://wiki.guildwars2.com/wiki/?search=<chatcode>`) in the default browser (ShellExecute-
    style; NOT a game hook). Works off the stored code; needs NO Decoder Ring. (Pretty-name wiki
    URLs are a v2 refinement.)
- **NO click-to-open-map / waypoint panning** — deliberately cut from v1 (would require game
  hooks). The "copy code, paste in chatbox, click there" path covers it via the game's native
  handling.

## Decoder Ring integration — follow its documented consumer contract EXACTLY

Read Decoder Ring's `docs/API.md` (§ Consumer lifetime contract) and `public/DecoderRingApi.h`
on master, and integrate against the REAL interface. Key rules (verify against the actual doc, this
is a summary):

- Resolution is **sync-if-warm + event-on-miss**: call the exported resolve function; a warm result
  returns immediately, a miss returns a not-ready sentinel and a completion event follows later.
  Match the event to the request via Decoder Ring's **correlation key** (read the doc for the exact
  scheme — reported as a `(linkType, id)` tuple).
- **Lifetime contract — critical (this is what prevents the unload crash):** do NOT cache Decoder
  Ring's exported function pointer across calls — re-resolve/re-validate the service is present
  before each use. Subscribe to its READY (appearance) and UNLOADING (disappearance) events and drop
  references on unload. Decoder Ring can load/unload at ANY time.
- **Graceful degradation:** if Decoder Ring is absent, Magpie must NOT crash and must still work —
  chips render with structural labels (from the vendored codec), and both context-menu actions
  (copy code, open wiki) still function (they use the raw stored code). Only resolved names/icons go
  missing. Detect absence and degrade silently; never block or crash.

## Build it in stages (so it comes together testably, not all-at-once)

1. **Skeleton:** new directory, Nexus addon scaffold mirroring Alter Ego (loads, registers a window,
   applies Alter Ego's theme, persists settings). Builds to a DLL, loads in Nexus, shows an empty
   window.
2. **Notes CRUD + two-pane shell + persistence:** create/select/delete notes, titles in the left
   pane, content in the right, save to disk, survive restart. Plain text only so far (no chips, no
   markdown render).
3. **View/edit modes + Save/Cancel + dirty-state modal.** Mode switching, edit-background tint,
   unsaved-changes guard.
4. **Markdown rendering (view mode):** bold/italic/headings(fontbig)/bullets. Edit mode still shows
   raw syntax. (No chips yet — render markdown around plain text.)
5. **Vendor the codec + edit-mode chips:** vendor `ChatLinks`+`SpecData.h`; adapt `ChipTextEdit`
   (multi-line, decoupled) so chat codes in edit mode become atomic chip cells showing structural
   labels (no Decoder Ring yet — structural only).
6. **Decoder Ring integration + rich chips:** wire resolution (warm + event), adapt the icon helper,
   render chips with icon + resolved name + tooltip in view mode (and resolved names on edit-mode
   chips), context menu (copy code / open wiki), and the full graceful-degradation path.

Each stage should build clean and be independently testable. This ordering means the whole notepad
works end-to-end (stages 1-4) before the hardest parts (chip editing, Decoder Ring) land, and a
problem in chips/resolution can't block the core notepad from functioning.

## Copy / UX writing (small but it matters — make it feel like the suite)

- Empty state (no notes yet): an inviting prompt to act, in the interface's voice, not a blank pane
  (e.g. something that invites creating the first note). You may give the notepad light "magpie"
  personality in flavour text (the addon is named for the bird that collects shiny things) — but
  keep all functional labels plain and clear (Save, Cancel, Create New Note). Don't overdo the
  theme; one or two light touches, not cutesy throughout.
- Degradation message when Decoder Ring is absent: clear and non-alarming — explain chips show basic
  labels without it, not an error. Never apologize, never vague.
- All rendered UI text ASCII (Nexus font constraint).

## Constraints (house style — same as your other addons)

- FROM SCRATCH in its own new directory. Standalone addon.
- Cross-compile to a Windows DLL via MinGW (mirror Alter Ego's CMake flow); include a host-test
  target on the Linux g++ target with no Nexus/ImGui include creep for the pure pieces (note model,
  markdown parsing, codec — these should be host-testable).
- **Never deploy the DLL.** Build it, run host tests, report ready; the user copies it into the
  addons folder. Do not write to the GW2 addons dir.
- Work on a feature branch; don't commit to `master` directly.
- NO game-memory RE, NO game-call hooks, NO map-pan. If a feature seems to need any of these, STOP —
  it's out of v1 scope by design.
- Vendored codec files byte-identical to Pie UI's canonical `src/chat/`, no-fork discipline.

## Explicitly OUT of scope (v2 or later — do NOT build)

- Share/export note codes (serialising a note for another user). v2.
- Right-click markdown format menu. v2.
- Click-waypoint-to-open-map / any game hook. Later, if ever.
- Pretty-name wiki URLs (use chat-code search URL for v1).
- Underline, tables, or any markdown beyond bold/italic/headings/bullets.

## Definition of done

1. Standalone addon builds to a DLL via MinGW (mirroring Alter Ego's build); loads in Nexus; uses
   Alter Ego's theme.
2. Two-pane notepad: create/select/delete notes, titles left, content right, persisted to disk,
   survives restart.
3. View/edit modes with edit-background tint; Save/Cancel; unsaved-changes modal on navigation.
4. Markdown rendering in view mode (bold/italic/headings→fontbig flat/bullets); raw syntax in edit
   mode; no format menu.
5. Chat-link chips: atomic in the edit buffer (adapted ChipTextEdit, multi-line, decoupled from
   RichLine); rich in view mode (icon + name + tooltip); right-click menu (copy code / open wiki),
   both working without Decoder Ring.
6. Decoder Ring integration follows the documented consumer lifetime contract (no cached pointer,
   re-validate per call, subscribe ready+unloading, drop on unload); resolves names/icons warm+event;
   degrades gracefully (structural labels, functional menu) when Decoder Ring is absent — never
   crashes.
7. Vendored codec byte-identical to upstream; RichLine/FrApi/game-hooks NOT brought across.
8. Host tests for the pure pieces (note model, markdown parse, codec) green; DLL builds clean.
9. Feature branch; DLL not deployed; short summary of structure, what was vendored vs adapted, and
   the staged build results.

## Process

Standard flow: brainstorm -> plan -> subagent-driven tasks with spec & code-quality review between
tasks. Start by reading Alter Ego (skeleton+theme), Pie UI's `src/chat/` (codec + ChipTextEdit +
SkillIconCache), and Decoder Ring's `docs/API.md` + `public/DecoderRingApi.h`, then present a plan
covering: the new directory/addon structure, the note data model + persistence, the markdown
parse/render approach, the ChipTextEdit adaptation (multi-line + decouple), the icon helper
adaptation, and the Decoder Ring integration against the real contract. WAIT for confirmation on
that plan before implementing, then build in the six stages above.
