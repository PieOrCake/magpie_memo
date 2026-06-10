# Magpie Memo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone Raidcore Nexus addon (Windows DLL, cross-compiled from Linux with MinGW) that is a notepad — multiple notes of multi-line markdown text with inline GW2 chat-link "chips" (icon + name + tooltip), resolved via the Decoder Ring addon, degrading gracefully when Decoder Ring is absent.

**Architecture:** Pure UI + data addon — no game-memory reading, no game-function hooks. A note is a single UTF-8 string (markdown + raw chat codes inline); chips and formatting are render-time interpretation only. The codec (`ChatLinks` + `SpecData.h`) is vendored byte-identical from Pie UI for offline structural decode; `ChipTextEdit` is adapted (made multi-line, repointed at Magpie's own chip resolver); the icon helper is adapted into a URL→texture cache; Decoder Ring is integrated against its real published contract (re-validate per call, never cache the pointer, subscribe ready+unloading).

**Tech Stack:** C++17, ImGui (vendored, same version as Alter Ego), Nexus addon API, nlohmann/json for persistence, MinGW cross-compile to DLL + native g++ host-test target. Skeleton and theme mirror Alter Ego.

---

## Resolved design decisions

- **Bold/italic rendering (no Nexus bold/italic font handle exists):** use render tricks, no bundled font assets. **Bold** = draw the glyphs twice with a 1px horizontal offset (the thickening trick Pie UI uses for skill numbers). *Italic* = shear the glyph quads (~12°) via a custom draw. **Headings** map to Nexus `FontBig` (flat — every heading level → `FontBig`, no per-level scaling). Normal text uses `FontUI`/`Font`. No `Fonts_AddFromMemory` of custom faces.
- **Icon download mechanism:** the adapted Magpie icon helper wraps Nexus's built-in `Textures_GetOrCreateFromURL` / `Textures_LoadFromURL` (Nexus does the HTTP download + GPU upload), keyed by a stable FNV-1a hash of the URL, with a failure set so a failed download is never poison-cached as "ready" (it is retryable, not treated as a successful empty texture). This honours "adapt Pie UI's `SkillIconCache::RequestUrl`" — same RequestUrl/Get/UrlKey shape — while dropping Pie UI's `/v2/skills` resolution (Decoder Ring already gives us the URL).
- **Persistence:** single JSON file `notes.json` in the addon's data directory (mirrors Alter Ego's `settings.json` + nlohmann pattern). One file holds all notes (ordered list of `{id, title, body}`).
- **Repo:** `magpie_memo/` is not yet a git repo. Stage 0 runs `git init`, commits the scaffold on `master`, then all feature work happens on a `feature/...` branch. The built DLL is **never** deployed to the game folder — the user deploys it.

---

## Decoder Ring real contract (verified against `public/DecoderRingApi.h` + `docs/API.md`)

- DataLink id `"DECODER_RING_API"` → `DecoderRingApi*` with `uint32_t apiVersion` (first field), `Resolve(uint8_t linkType, uint32_t id, const char* chatCode, DecoderRecord* out)`, `QueryPrice(uint32_t itemId, DecoderPrice* out)`.
- `DECODER_RING_API_VERSION == 1`. `DecoderRecord.schemaVersion` is the first field; check it on every record read.
- Events: `EV_DECODER_RING_READY`, `EV_DECODER_RING_UNLOADING`, `EV_DECODER_RING_PING`, `EV_DECODER_RING_RESOLVED` (payload `DecoderRecord*`, valid only during the handler — copy out).
- `Resolve` returns `DR_NotReady` (fetch kicked, watch event), `DR_Resolved` (record filled), or `DR_Failed` (cooldown, retryable). Correlation key = `(linkType, id)`. Build links (`0x0D`) **require** the `chatCode` string; item/skin/skill/waypoint may pass it as the full `[&...]` anyway (harmless).
- **Lifetime rules (mandatory):** never cache the `DecoderRingApi*`; re-resolve via a `GetDecoder()` accessor (DataLink_Get + `apiVersion == 1` check) immediately before every call. Subscribe READY (refresh "present" indicator) and UNLOADING (drop cached records + pending keys). Presence indicator must be live (`GetDecoder() != nullptr` each frame), not latched. Absent service = a rendering mode (structural labels), never a crash.
- `iconUrl` is a URL only — Magpie downloads + uploads the texture itself.

---

## File / directory structure (`/home/tony/Dev/magpie_memo/`)

```
CMakeLists.txt              # MinGW DLL target (mirror Alter Ego) + native host-test target
MagpieMemo.def              # DLL export of GetAddonDef
include/nexus/Nexus.h       # copied from Alter Ego (unchanged)
include/nlohmann/json.hpp   # copied from Alter Ego (unchanged)
public/DecoderRingApi.h     # copied byte-identical from decoder_ring/public (the integration header)
lib/imgui/...               # copied from Alter Ego (same ImGui version)
src/
  dllmain.cpp               # Nexus lifecycle: GetAddonDef/Load/Unload/Render/Options, theme, window, settings glue
  Theme.{h,cpp}             # Alter Ego accent palette + helpers (gold border, green/amber buttons, dark frame bg)
  NotesStore.{h,cpp}        # PURE: note model (id/title/body), CRUD, JSON (de)serialise. Host-testable, no Nexus/ImGui.
  Markdown.{h,cpp}          # PURE: parse a note body line into spans (heading/bullet/bold/italic/text/chip). Host-testable.
  MarkdownRender.{h,cpp}    # ImGui rendering of parsed markdown (uses Theme, fonts, bold/italic tricks, chip draw)
  ChipResolver.{h,cpp}      # Magpie's own chip name/colour/icon/tooltip resolution: vendored codec + Decoder Ring
  DecoderClient.{h,cpp}     # Decoder Ring consumer: GetDecoder(), Resolve, event handlers, record cache, pending keys
  IconCache.{h,cpp}         # adapted icon helper: RequestUrl/Get/UrlKey over Nexus Textures_*FromURL, failure set
  ChipTextEdit.{h,cpp}      # ADAPTED from Pie UI: multi-line, decoupled from RichLine -> calls ChipResolver
  NotesWindow.{h,cpp}       # two-pane master/detail UI, view/edit modes, save/cancel, dirty-state modal, empty state
  chat/
    ChatLinks.h             # VENDORED byte-identical from pie_ui/src/chat
    ChatLinks.cpp           # VENDORED byte-identical from pie_ui/src/chat
    SpecData.h              # VENDORED byte-identical from pie_ui/src/chat (include exactly once)
tests/
  host_tests_main.cpp       # native g++ test driver (assert-based, mirrors pie_ui chatlinks_isolation)
plans/2026-06-11-magpie-memo.md
handover.md                 # (gitignored; created at session end)
.gitignore                  # build/, *.dll, *.lib, claude.md, handover.md, LLM files
```

**Vendored (byte-identical, no-fork):** `chat/ChatLinks.{h,cpp}`, `chat/SpecData.h`. **Adapted:** `ChipTextEdit.{h,cpp}`, `IconCache.{h,cpp}`. **NOT brought across:** `RichLine`, `RichLineResolveChip`, `FrApiHook`, any game-memory/hook code.

---

## Stage 0 — Repo + scaffold

### Task 0: Initialise repo and project skeleton directories

**Files:** Create `/home/tony/Dev/magpie_memo/.gitignore`, directory tree above (empty dirs as needed).

- [ ] **Step 1:** `cd /home/tony/Dev/magpie_memo && git init`
- [ ] **Step 2:** Write `.gitignore` (build artefacts, `*.dll`, `*.lib`, `claude.md`, `handover.md`, any LLM files — per house rule LLM files are always gitignored).
- [ ] **Step 3:** Copy `include/nexus/Nexus.h`, `include/nlohmann/json.hpp`, and the `lib/imgui/` tree from `/home/tony/Dev/alter_ego` (same versions). Copy `public/DecoderRingApi.h` byte-identical from `/home/tony/Dev/decoder_ring/public/DecoderRingApi.h`.
- [ ] **Step 4:** Commit scaffold on `master`: `git add -A && git commit -m "chore: project scaffold (nexus/imgui/json/decoder header)"`
- [ ] **Step 5:** `git checkout -b feature/magpie-memo` — all subsequent work lands here.

---

## Stage 1 — Skeleton: loads, registers a window, applies Alter Ego's theme

### Task 1: CMake — DLL target + host-test target

**Files:** Create `CMakeLists.txt`, `MagpieMemo.def`.

- [ ] **Step 1:** Author `CMakeLists.txt` mirroring Alter Ego's MinGW cross-compile block (`CMAKE_SYSTEM_NAME Windows`, `x86_64-w64-mingw32-g++`, C++17, `-Os -DNDEBUG`, static libgcc/libstdc++, `WIN32_LEAN_AND_MEAN`), building `MagpieMemo` SHARED → `MagpieMemo.dll` (PREFIX "", SUFFIX ".dll"), linking `gdi32 wininet shell32 comdlg32`, with the `.def` and `--strip-all`. Include the ImGui sources. Add a **separate native host-test executable** target (no MinGW, no ImGui/Nexus includes) compiling `NotesStore.cpp`, `Markdown.cpp`, `chat/ChatLinks.cpp`, and `tests/host_tests_main.cpp` — guarded so it only builds when invoked on the host toolchain (mirror pie_ui's `build-tests/chatlinks_isolation` separation: a second build dir / option, e.g. `-DMAGPIE_HOST_TESTS=ON` using the host g++ rather than the MinGW toolchain).
- [ ] **Step 2:** `MagpieMemo.def` exports `GetAddonDef`.
- [ ] **Step 3:** Commit.

**Acceptance:** `cmake` configures both targets; the DLL target compiles an empty `dllmain.cpp` stub; the host-test target compiles (no Nexus/ImGui creep into the pure files).

### Task 2: Nexus lifecycle skeleton

**Files:** Create `src/dllmain.cpp`, `src/Theme.{h,cpp}`.

- [ ] **Step 1:** Implement the standard Nexus entry points mirroring Alter Ego: `BOOL DllMain`, `extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()` returning a filled `AddonDefinition_t` (signature, name "Magpie Memo", author "PieOrCake", description, `AddonLoad`/`AddonUnload`, flags, provider), and `AddonLoad(AddonAPI_t*)` / `AddonUnload()` / `AddonRender()` / `AddonOptions()`.
- [ ] **Step 2:** In `AddonLoad`: store `APIDefs`, `GUI_Register(RT_Render, AddonRender)`, `GUI_Register(RT_OptionsRender, AddonOptions)`, load an embedded icon via `Textures_LoadFromMemory`, `QuickAccess_Add`, register a toggle keybind via `InputBinds_RegisterWithString`, `GUI_RegisterCloseOnEscape("Magpie Memo", &g_WindowVisible)`. In `AddonUnload`: unregister/unsubscribe everything.
- [ ] **Step 3:** `Theme.{h,cpp}` captures Alter Ego's accent palette as named constants/helpers (gold border `ImVec4(0.90,0.75,0.25,~0.9)`, green action button set, amber secondary button set, dark frame bg) and small RAII push/pop helpers. `AddonRender` draws an empty `ImGui::Begin("Magpie Memo", &g_WindowVisible)` window styled with the theme. The Magpie addon inherits Nexus's default ImGui style (same as Alter Ego — Alter Ego has no global `ApplyTheme`; the "theme" is the inherited Nexus style plus this consistent accent palette applied per-widget).
- [ ] **Step 4:** Build the DLL clean. Commit.

**Acceptance (manual, user-run in game):** DLL loads in Nexus, quick-access icon toggles an empty themed window that visually matches the suite. *(Builder reports "ready to load"; the user loads it — never deploy.)*

---

## Stage 2 — Notes CRUD + two-pane shell + persistence (plain text)

### Task 3: NotesStore (PURE, host-tested)

**Files:** Create `src/NotesStore.{h,cpp}`, `tests/host_tests_main.cpp`.

Interface:
```cpp
namespace Magpie {
struct Note { std::string id; std::string title; std::string body; };
class NotesStore {
public:
    const std::vector<Note>& Notes() const;
    Note&  Create(const std::string& defaultTitle = "Untitled note"); // returns the new note (appended)
    void   Delete(const std::string& id);
    Note*  Get(const std::string& id);            // nullptr if missing
    std::string ToJson() const;                   // serialise all notes (ordered)
    bool   FromJson(const std::string& json);     // replace all; false on parse error (keeps prior state)
};
} // namespace Magpie
```

- [ ] **Step 1:** Write failing host tests in `tests/host_tests_main.cpp`: create→appears in `Notes()` with default title and unique id; edit title/body via `Get`; delete removes it; `ToJson` then `FromJson` round-trips an ordered multi-note set; `FromJson` on garbage returns false and leaves state untouched.
- [ ] **Step 2:** Build host-tests target, run, verify FAIL.
- [ ] **Step 3:** Implement `NotesStore` (ids = monotonic or random hex string; nlohmann json array of `{id,title,body}`). No Nexus/ImGui includes.
- [ ] **Step 4:** Run host-tests, verify PASS.
- [ ] **Step 5:** Commit.

### Task 4: Persistence glue + two-pane shell

**Files:** Create `src/NotesWindow.{h,cpp}`; modify `src/dllmain.cpp`.

- [ ] **Step 1:** `NotesWindow` owns a `NotesStore`, a selected-note id, and Save/Load that read/write `notes.json` in the addon data dir (path via the same Nexus data-directory accessor Alter Ego uses for `settings.json`). Load on `AddonLoad`, save on change + on `AddonUnload`.
- [ ] **Step 2:** Render two panes inside the window: left = list of note **titles** (selectable) + a **"Create New Note"** button at the bottom; right = the selected note's body shown as plain multi-line text for now (no markdown/chips yet). A right-click or button to **delete** a note.
- [ ] **Step 3:** Empty state (no notes): an inviting, lightly "magpie"-flavoured prompt to create the first note (functional labels stay plain). ASCII only.
- [ ] **Step 4:** Build DLL clean. Commit.

**Acceptance (manual):** create/select/delete notes; titles left, body right; persists across restart.

---

## Stage 3 — View/edit modes + Save/Cancel + dirty-state modal

### Task 5: Mode switching + dirty guard

**Files:** Modify `src/NotesWindow.{h,cpp}`.

- [ ] **Step 1:** Add a per-window `Mode { View, Edit }` and an edit buffer (plain `std::string` for now) plus a `dirty` flag. View = render body; Edit = editable raw text in an `InputTextMultiline`, with a **different background tint** (edit mode is visually distinct). Creating a new note opens directly in **Edit** mode.
- [ ] **Step 2:** In Edit mode show **Save** and **Cancel** buttons at the top. Save → write buffer to the note + persist + return to View. Cancel → discard buffer + return to View. Track `dirty` = buffer differs from stored body.
- [ ] **Step 3:** **Dirty-state modal:** if `dirty` and the user selects a different note (or creates one), open a modal "This note has unsaved changes" with **Save / Discard / Cancel** (Cancel aborts the navigation). Never silently lose edits.
- [ ] **Step 4:** Build DLL clean. Commit.

**Acceptance (manual):** edit tint visible; Save/Cancel behave; switching notes with unsaved edits triggers the modal and honours each choice.

---

## Stage 4 — Markdown rendering (view mode)

### Task 6: Markdown parser (PURE, host-tested)

**Files:** Create `src/Markdown.{h,cpp}`; modify `tests/host_tests_main.cpp`.

Interface (line-oriented; whole-line block kind + inline spans). Chips are NOT resolved here — the parser only emits raw chat-code spans; resolution is a render-time concern.
```cpp
namespace Magpie::Md {
enum class Block { Paragraph, Heading, Bullet };
enum class Inline { Text, Bold, Italic, Chip };  // Chip span carries the raw "[&...]" code
struct Span  { Inline kind; std::string text; }; // text = literal text, or the raw chat code for Chip
struct Line  { Block block; std::vector<Span> spans; }; // headings store level only to NOT rewrite source
std::vector<Line> Parse(const std::string& body);        // splits on '\n'
}
```

- [ ] **Step 1:** Failing host tests: `# H` / `### H` → `Heading` (any level, flat); `- item` → `Bullet`; `**b**` → one `Bold` span; `*i*` → one `Italic` span; mixed inline (`a **b** c`) → ordered Text/Bold/Text spans; a line containing `[&...]` → a `Chip` span carrying the exact code, with surrounding text as Text spans; plain line → `Paragraph` with one Text span. Use `ChatLinks::SegmentLine`-compatible expectations for where chip spans fall.
- [ ] **Step 2:** Build host-tests, verify FAIL.
- [ ] **Step 3:** Implement `Parse`. For inline splitting, reuse the vendored `ChatLinks::SegmentLine` to locate chat-code spans first, then apply bold/italic markdown tokenisation to the text runs between them. Headings/bullets are whole-line. Do not rewrite the user's source (store heading level, don't normalise it).
- [ ] **Step 4:** Run host-tests, verify PASS. Commit.

### Task 7: Markdown rendering (ImGui, view mode)

**Files:** Create `src/MarkdownRender.{h,cpp}`; modify `src/NotesWindow.cpp`.

- [ ] **Step 1:** `RenderBody(const std::vector<Md::Line>&, ...)` draws each line: Heading → push `FontBig` (flat for all levels); Bullet → indent + marker; inline Text normal; **Bold** = double-draw at +1px X offset; *Italic* = sheared glyph quads (custom `AddText`/quad shear). Chip spans render as plain bracketed text placeholders for now (real chips arrive Stage 6). All rendered text ASCII.
- [ ] **Step 2:** Wire View mode to `Md::Parse` + `RenderBody`. Edit mode still shows the **raw** markdown syntax (no rendering) per "edit=source, view=preview".
- [ ] **Step 3:** Build DLL clean. Commit.

**Acceptance (manual):** view mode renders bold/italic/headings/bullets; edit mode shows raw syntax.

---

## Stage 5 — Vendor codec + edit-mode chips (structural only)

### Task 8: Vendor the codec

**Files:** Create `src/chat/ChatLinks.h`, `src/chat/ChatLinks.cpp`, `src/chat/SpecData.h` (byte-identical copies from `pie_ui/src/chat/`); add `ChatLinks.cpp` to both CMake targets.

- [ ] **Step 1:** Copy the three files byte-identical (`#pragma once` intact, no edits — no-fork discipline). `SpecData.h` included exactly once.
- [ ] **Step 2:** Add codec host tests: `DetectType`/`SegmentLine`/`LinkTypeLabel` on sample chat codes (e.g. an item, a waypoint, a build code → `[Item]`/`[Waypoint]`/real spec label from `SpecData.h`). Confirm offline (no service) labelling works.
- [ ] **Step 3:** Build host-tests + DLL clean. Commit.

### Task 9: ChipResolver (structural tier)

**Files:** Create `src/ChipResolver.{h,cpp}`.

Interface — the single seam `ChipTextEdit` and the renderer both call:
```cpp
namespace Magpie {
struct ChipView { std::string label; ImU32 color; /* icon + tooltip added Stage 6 */ };
ChipView ResolveChip(const std::string& chatCode);   // structural-only for now
}
```
- [ ] **Step 1:** Implement `ResolveChip` using only the vendored codec: `DetectType` → `LinkTypeLabel` (`[Item]`, `[Waypoint]`, …) and, for build links, the `SpecData.h` spec label (e.g. `[Mirage Build]`). Colour per link type (a small palette). No Decoder Ring yet.
- [ ] **Step 2:** Commit.

### Task 10: Adapt ChipTextEdit → multi-line, decoupled

**Files:** Create `src/ChipTextEdit.{h,cpp}` (adapted from `pie_ui/src/widgets/ChipTextEdit.{h,cpp}`); modify `src/NotesWindow.cpp` to use it as the Edit-mode buffer.

- [ ] **Step 1:** Copy `ChipTextEdit.{h,cpp}` in, then **decouple**: replace the three `RichLineResolveChip(code, name, color)` call sites with `Magpie::ResolveChip(code)` → fill `ChipCell.name`/`.color`. Remove the `#include "chat/RichLine.h"`. Do **not** bring `RichLine`/`RichLineResolveChip`/`FrApiHook` across.
- [ ] **Step 2:** **Make it multi-line.** Pie UI's `ChipTextEdit` is single-line stb_textedit; a note is multi-line. Introduce newline handling: newline as a cell, multi-row layout (row breaking + per-row y), caret/selection/mouse across rows, vertical scroll. This is the highest-risk task — derive the implementation against the real `ChipTextEdit.cpp` (344 lines) and stb_textedit's row-callback model; keep chips atomic (caret steps over a chip as one unit; one backspace removes a whole chip). Keyboard capture/focus handling stays as in the original (`ChipInputActive`, blur-on-outside-click).
- [ ] **Step 2a:** Where practical, factor the cell/line-model logic so a slice is host-testable (e.g. SetText parses `[&..]` into chip cells; GetText reassembles codes+text in order — round-trip testable without ImGui). Add that round-trip host test.
- [ ] **Step 3:** In Edit mode, chat codes show as **atomic chip cells** with structural labels (markdown still shown as raw syntax around them). Build DLL clean. Commit.

**Acceptance (manual):** in edit mode, typing/pasting a `[&...]` code becomes one atomic chip showing its structural label; backspace removes the whole chip; multi-line editing (caret up/down, selection across lines) works.

---

## Stage 6 — Decoder Ring integration + rich chips

### Task 11: DecoderClient (consumer lifetime contract)

**Files:** Create `src/DecoderClient.{h,cpp}`; modify `src/dllmain.cpp` (subscribe/raise on load, unsubscribe on unload).

Interface:
```cpp
namespace Magpie {
struct ResolvedRecord { /* copy of the fields we use from DecoderRecord */ bool valid=false; ... };
namespace Decoder {
  void Init(AddonAPI_t* api);   // subscribe READY/UNLOADING/RESOLVED, raise PING
  void Shutdown();              // unsubscribe all
  bool Present();               // GetDecoder() != nullptr, live (each frame)
  // Warm-or-event resolve. Returns a cached record if warm; else kicks a fetch,
  // stores the (linkType,id) pending key, and returns {valid=false} until the event lands.
  const ResolvedRecord* Resolve(uint8_t linkType, uint32_t id, const char* chatCode);
}
}
```
- [ ] **Step 1:** Implement `GetDecoder()` exactly per the contract: `DataLink_Get("DECODER_RING_API")` + `apiVersion == DECODER_RING_API_VERSION` check; **never cache** the pointer. `Resolve` calls `decoder->Resolve(...)`; on `DR_Resolved` cache a `ResolvedRecord`; on `DR_NotReady` store the `(linkType,id)` pending key; on `DR_Failed` mark retryable.
- [ ] **Step 2:** Event handlers: `OnResolved(DecoderRecord*)` checks `schemaVersion`, matches pending `(linkType,id)`, copies the record into the cache (pointer only valid during the call). `OnUnloading` drops all cached records + pending keys + flips presence. `OnReady` refreshes presence. In `AddonLoad`, subscribe all three + raise `EV_DECODER_RING_PING`; in `AddonUnload`, unsubscribe.
- [ ] **Step 3:** Build DLL clean. Commit.

### Task 12: IconCache (adapted icon helper)

**Files:** Create `src/IconCache.{h,cpp}`.

Interface (mirrors `SkillIconCache`'s URL path):
```cpp
namespace Magpie::Icons {
  void       Init(AddonAPI_t* api);
  uint32_t   UrlKey(const char* url);            // FNV-1a 32-bit
  void       RequestUrl(const char* url);        // async download+upload via Nexus Textures_*FromURL
  Texture_t* Get(const char* url);               // nullptr until ready; never returns a failed icon as ready
}
```
- [ ] **Step 1:** Implement over Nexus `Textures_GetOrCreateFromURL`/`Textures_LoadFromURL` (split the icon URL into remote-host + endpoint). Maintain a `key→Texture_t*` map and a **failed set** so a failed download is retryable, never poison-cached as a valid empty texture. Keep it a standalone unit (the consumer-side counterpart to Decoder Ring).
- [ ] **Step 2:** Commit.

### Task 13: Rich chips (view + edit) + context menu + tooltips + degradation

**Files:** Modify `src/ChipResolver.{h,cpp}`, `src/MarkdownRender.cpp`, `src/ChipTextEdit.cpp`, `src/NotesWindow.cpp`.

- [ ] **Step 1:** Upgrade `ResolveChip` to a richer view: structural label (from codec, always) + optional resolved name/iconUrl/tooltip facts from `Decoder::Resolve`. Name shows the structural label until the resolved name arrives, then upgrades. Extend `ChipView` with `iconUrl`, `tooltip` data, and the raw `chatCode`.
- [ ] **Step 2:** **View-mode chips:** draw icon (via `Icons::Get(iconUrl)`, text-only fallback while loading/failed) + name; chip always renders and stays functional.
- [ ] **Step 3:** **Tooltip on hover:** ImGui tooltip from the Decoder record (name + type-specific extras: item vendor value, skill description/facts, waypoint map). Magpie owns the tooltip.
- [ ] **Step 4:** **Right-click context menu** (both view and edit chips): **"Copy chat code"** (copies the stored raw code via the Windows clipboard — works with no Decoder Ring) and **"Open in wiki"** (`ShellExecute` to `https://wiki.guildwars2.com/wiki/?search=<chatcode>` — works with no Decoder Ring). No game hooks, no map-pan.
- [ ] **Step 5:** **Graceful degradation:** when `Decoder::Present()` is false, chips render structural labels, both context-menu actions still work, and a clear, non-alarming message explains chips show basic labels without Decoder Ring (Options pane and/or a subtle in-window note). Live presence indicator (re-evaluated each frame). Never block or crash.
- [ ] **Step 6:** Edit-mode chips also upgrade to resolved names when available (still atomic cells). Build DLL clean. Commit.

**Acceptance (manual):** with Decoder Ring loaded, chips show icon + resolved name + tooltip and the wiki/copy menu; with Decoder Ring disabled mid-session, chips drop to structural labels, the menu still works, and nothing crashes; re-enabling restores rich chips.

---

## Host-test target details

- Mirror pie_ui's `build-tests/chatlinks_isolation`: a native (host g++, NOT MinGW) executable compiling only the **pure** units — `NotesStore.cpp`, `Markdown.cpp`, `chat/ChatLinks.cpp` (+ the ChipTextEdit cell-model slice if factored) — and `tests/host_tests_main.cpp` (assert-based, returns non-zero on failure). No Nexus/ImGui/Win32 includes in those pure units. Build via a separate build dir / CMake option so the host compiler is used. Green host tests are part of Definition of Done.

---

## Definition-of-Done coverage check

| DoD item | Task(s) |
|---|---|
| DLL builds via MinGW, loads, Alter Ego theme | 1, 2 |
| Two-pane CRUD + persistence + survives restart | 3, 4 |
| View/edit + tint + Save/Cancel + unsaved modal | 5 |
| Markdown view (bold/italic/headings→FontBig flat/bullets), raw in edit | 6, 7 |
| Chips atomic in edit (adapted ChipTextEdit, multi-line, decoupled); rich in view (icon+name+tooltip); right-click copy/open-wiki without Decoder Ring | 8, 9, 10, 13 |
| Decoder Ring contract (no cached ptr, re-validate per call, ready+unloading, drop on unload); warm+event; graceful degradation, never crashes | 11, 12, 13 |
| Vendored codec byte-identical; RichLine/FrApi/hooks NOT brought across | 8, 10 |
| Host tests for pure pieces green; DLL builds clean | 1, 3, 6, 8, host-test target |
| Feature branch; DLL not deployed; summary of structure/vendored-vs-adapted/staged results | 0, final report |

---

## Notes for the implementer

- **Never deploy the DLL.** Build it, run host tests, report ready — the user copies it into the addons folder.
- **No-fork discipline:** vendored `ChatLinks`/`SpecData.h` stay byte-identical to `pie_ui/src/chat/`. If a codec bug surfaces, fix upstream in Pie UI and re-vendor — do not edit the copy.
- **No game-memory RE, no game-call hooks, no map-pan.** If a feature seems to need any, STOP — it is out of v1 scope by design.
- Read the real reference code at implementation time; this plan cites interfaces but the ChipTextEdit multi-line adaptation must be derived against the actual 344-line source.
