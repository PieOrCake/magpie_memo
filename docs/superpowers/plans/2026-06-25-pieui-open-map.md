# Pie UI "Open Map" Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Left-clicking a waypoint chip in a saved Magpie Memo note asks the optional Pie UI addon to open and pan the in-game world map to that waypoint, with full graceful fallback when Pie UI is absent.

**Architecture:** A new optional-dependency client `Magpie::PieUi` mirrors the existing `Magpie::Decoder` pattern but simpler — a latched `std::atomic<bool>` presence flag (set by the `EV_PIE_UI_READY` event), no cache, no unloading handler. `DrawRichChip` gains a left-click handler, a tooltip line, and a context-menu item for waypoint chips, all gated on `PieUi::Present()`. Magpie only hands Pie UI the raw `[&...]` chat code over the Nexus event bus; Pie UI owns the decode and the game call.

**Tech Stack:** C++17, MinGW cross-compile to a Windows DLL, Raidcore Nexus addon API (`AddonAPI_t` event bus), Dear ImGui for the chip UI.

## Global Constraints

- **No hard dependency on Pie UI.** Magpie Memo must load and run fully with Pie UI absent. Raising into an absent Pie UI is a harmless no-op.
- **Never modify the Pie UI project.** Implement only the Magpie Memo side. There is no Pie UI header to vendor — the `EV_*` event-name string constants are defined on our side.
- **All rendered / tooltip / in-game text must be ASCII** (Nexus font is ASCII-only) — no em-dashes, arrows, or Unicode.
- **Match surrounding code** style, naming, comment density, and the `Magpie::` namespace convention. Reuse the vendored `ChatLinks` codec — do not fork it.
- **Do not bump the version** — the four `V_*` defines in `src/dllmain.cpp` stay as-is unless the user says otherwise.
- **Do not deploy the DLL** into the game's addons folder — the user deploys it themselves. Building is fine.
- Pie UI contract event names (verbatim): `EV_PIEUI_OPEN_MAP` (Magpie raises; payload = null-terminated `"[&...]"` C string via `void*`), `EV_PIE_UI_PING` (Magpie raises on load), `EV_PIE_UI_READY` (Pie UI raises; Magpie subscribes + latches present). Nexus delivers events synchronously, so a pointer to a local string's `c_str()` is valid for the duration of the raise.
- Waypoint link type is `0x04` (`PieUI::ChatLinks::LINK_MAP`).

---

## File Structure

- **Create `src/PieUiClient.h`** — the `Magpie::PieUi` client interface (`Init`/`Shutdown`/`Present`/`OpenMap`).
- **Create `src/PieUiClient.cpp`** — implementation: file-local `APIDefs`, `EV_*` string constants, `std::atomic<bool>` latch, event subscribe/ping/raise.
- **Modify `CMakeLists.txt`** — add `src/PieUiClient.cpp` to the DLL `SOURCES` only (not host tests).
- **Modify `src/dllmain.cpp`** — include the header; `PieUi::Init` in `AddonLoad`, `PieUi::Shutdown` in `AddonUnload`.
- **Modify `src/ChipRich.cpp`** — include the header; add the waypoint left-click handler, tooltip line, and context-menu item in `DrawRichChip`.

**Testing reality (read before starting):** `PieUiClient` and `ChipRich` are **non-pure** units — they depend on `AddonAPI_t` and ImGui, so they are not in the host-test target (`magpie_host_tests` builds only the pure units). There is therefore no unit test to write here, exactly as there is none for `DecoderClient`. The per-task verification gate is: **(a) the DLL compiles and links** via the MinGW build, and **(b) the existing host-test suite still builds and passes** (regression guard). Functional verification is the in-game manual pass in the final task. Do not invent fake unit tests for non-pure code.

**Build commands (used throughout):**
- DLL (re-runs cmake automatically when `CMakeLists.txt` changed): `cmake --build build -j`
- Host tests: `cmake --build build-host -j && ./build-host/magpie_host_tests`

If `build/` or `build-host/` is missing, configure once first:
- `cmake -S . -B build`
- `cmake -S . -B build-host -DMAGPIE_HOST_TESTS=ON`

---

### Task 1: PieUiClient unit + build

**Files:**
- Create: `src/PieUiClient.h`
- Create: `src/PieUiClient.cpp`
- Modify: `CMakeLists.txt` (DLL `SOURCES` list, after `src/DecoderClient.cpp` on line 60)

**Interfaces:**
- Consumes: `AddonAPI_t*` (from `nexus/Nexus.h`) — provides `Events_Subscribe`, `Events_Unsubscribe`, `Events_Raise`.
- Produces (later tasks rely on these exact signatures):
  - `void Magpie::PieUi::Init(AddonAPI_t* api);`
  - `void Magpie::PieUi::Shutdown();`
  - `bool Magpie::PieUi::Present();`
  - `void Magpie::PieUi::OpenMap(const std::string& chatCode);`

- [ ] **Step 1: Create `src/PieUiClient.h`**

```cpp
#pragma once
#include "nexus/Nexus.h"
#include <string>

namespace Magpie { namespace PieUi {

// Subscribe EV_PIE_UI_READY, then raise EV_PIE_UI_PING. Call from AddonLoad
// after APIDefs is set.
void Init(AddonAPI_t* api);

// Unsubscribe the READY handler and clear APIDefs. Call from AddonUnload before
// APIDefs is nulled.
void Shutdown();

// Latched presence: true once EV_PIE_UI_READY has been seen this session. Pie UI
// has no "unloading" event, so this latch can go stale if Pie UI unloads
// mid-session — acceptable, because OpenMap() is a fire-and-forget no-op when
// Pie UI is not actually listening (never a crash).
bool Present();

// Ask Pie UI to open + pan the world map to the waypoint encoded in `chatCode`
// (a full "[&...]" string). Raises EV_PIEUI_OPEN_MAP with the C string as
// payload. Safe to call when Pie UI is absent — the raise is then a no-op.
void OpenMap(const std::string& chatCode);

}} // namespace Magpie::PieUi
```

- [ ] **Step 2: Create `src/PieUiClient.cpp`**

```cpp
// ============================================================================
// PieUiClient — the ONLY place that talks to the optional Pie UI provider.
// Magpie is here the CONSUMER of Pie UI's "open world map" service, which is
// fire-and-forget (no data returned), so this client is much simpler than
// DecoderClient: a single latched presence flag, no cache, no unloading
// handler.
//
// Contract (Pie UI ships its side):
//   * EV_PIE_UI_PING   — we raise this on load to probe for Pie UI.
//   * EV_PIE_UI_READY  — Pie UI raises this on its own load and in reply to a
//                        ping; we subscribe and latch "present".
//   * EV_PIEUI_OPEN_MAP— we raise this with a "[&...]" chat code; Pie UI decodes
//                        the PoI id and opens + pans the map. Strictly
//                        validated on Pie UI's side; a bad/non-waypoint link is
//                        ignored silently. No reply.
//
// The READY handler may fire off the render thread, so presence is a
// std::atomic<bool>. Pie UI has no "unloading" event; a stale latch can at worst
// show an affordance that becomes a no-op raise — never a crash.
//
// We never touch the Pie UI project, so there is no header to vendor — the
// event-name string constants live here.
// ============================================================================
#include "PieUiClient.h"

#include <atomic>

namespace Magpie { namespace PieUi {

namespace {

AddonAPI_t* APIDefs = nullptr;

// Pie UI event identifiers (our copy of the contract; Pie UI is never modified).
constexpr const char* EV_PIEUI_OPEN_MAP = "EV_PIEUI_OPEN_MAP";  // we raise
constexpr const char* EV_PIE_UI_PING    = "EV_PIE_UI_PING";     // we raise
constexpr const char* EV_PIE_UI_READY   = "EV_PIE_UI_READY";    // we subscribe

// Latched once Pie UI announces readiness. The READY handler may run on a
// non-render thread, hence atomic.
std::atomic<bool> s_present{false};

// Pie UI (re)announced readiness — latch present. void(*)(void*) == EVENT_CONSUME.
void OnReady(void*) {
    s_present.store(true);
}

} // namespace

void Init(AddonAPI_t* api) {
    APIDefs = api;
    if (!APIDefs) return;

    // Subscribe first, then ping — catches a Pie UI that loaded before us (it
    // replies to the ping with READY) as well as one that loads later (it raises
    // READY on its own load).
    APIDefs->Events_Subscribe(EV_PIE_UI_READY, OnReady);
    APIDefs->Events_Raise(EV_PIE_UI_PING, nullptr);
}

void Shutdown() {
    if (APIDefs) {
        // Unsubscribe the SAME function pointer we subscribed.
        APIDefs->Events_Unsubscribe(EV_PIE_UI_READY, OnReady);
    }
    APIDefs = nullptr;
    // s_present intentionally left as-is: harmless, and Shutdown runs at unload.
}

bool Present() {
    return s_present.load();
}

void OpenMap(const std::string& chatCode) {
    if (!APIDefs || !APIDefs->Events_Raise || chatCode.empty()) return;
    // Nexus delivers synchronously, so the c_str() pointer is valid for the raise.
    APIDefs->Events_Raise(EV_PIEUI_OPEN_MAP, (void*)chatCode.c_str());
}

}} // namespace Magpie::PieUi
```

- [ ] **Step 3: Add the source to `CMakeLists.txt`**

In the `set(SOURCES ...)` block, add the new file immediately after the `src/DecoderClient.cpp` line (line 60):

```cmake
        src/DecoderClient.cpp
        src/PieUiClient.cpp
        src/IconUrl.cpp
```

- [ ] **Step 4: Build the DLL — verify it compiles and links**

Run: `cmake --build build -j`
Expected: build succeeds, ends producing `build/MagpieMemo.dll` with no errors. (`PieUiClient` is currently unreferenced — that is fine; it still compiles and links into the DLL.)

- [ ] **Step 5: Build + run the host tests — verify no regression**

Run: `cmake --build build-host -j && ./build-host/magpie_host_tests`
Expected: builds clean and prints `ALL PASS` (the host target is unchanged; this confirms nothing broke).

- [ ] **Step 6: Commit**

```bash
git add src/PieUiClient.h src/PieUiClient.cpp CMakeLists.txt
git commit -m "feat: add PieUiClient (optional Pie UI open-map consumer)"
```

---

### Task 2: Wire PieUiClient into the addon lifecycle

**Files:**
- Modify: `src/dllmain.cpp` (include near line 15; `Init` in `AddonLoad` near line 146; `Shutdown` in `AddonUnload` near line 160)

**Interfaces:**
- Consumes: `Magpie::PieUi::Init`, `Magpie::PieUi::Shutdown` (from Task 1).
- Produces: nothing new — this only activates the client.

- [ ] **Step 1: Add the include**

Next to the existing `#include "DecoderClient.h"` (line 15), add:

```cpp
#include "DecoderClient.h"
#include "PieUiClient.h"
```

- [ ] **Step 2: Initialise in `AddonLoad`**

Immediately after the Decoder Ring init block (line 146, `Magpie::Decoder::Init(APIDefs);`), add:

```cpp
    // Decoder Ring consumer client (optional provider; lifetime-contract safe).
    Magpie::Decoder::Init(APIDefs);

    // Pie UI consumer client (optional provider; latched presence, no-op when absent).
    Magpie::PieUi::Init(APIDefs);
```

- [ ] **Step 3: Shut down in `AddonUnload`**

Immediately after the Decoder Ring shutdown (line 160, `Magpie::Decoder::Shutdown();`), add:

```cpp
    // Drop Decoder Ring subscriptions + cache before APIDefs goes away.
    Magpie::Decoder::Shutdown();

    // Unsubscribe Pie UI's READY handler before APIDefs goes away.
    Magpie::PieUi::Shutdown();
```

- [ ] **Step 4: Build the DLL — verify it still compiles and links**

Run: `cmake --build build -j`
Expected: build succeeds, `build/MagpieMemo.dll` produced, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/dllmain.cpp
git commit -m "feat: init/shutdown PieUiClient in addon lifecycle"
```

---

### Task 3: Waypoint chip click affordance in DrawRichChip

**Files:**
- Modify: `src/ChipRich.cpp` (include near line 26; the interaction block of `DrawRichChip`, lines ~352-385)

**Interfaces:**
- Consumes: `Magpie::PieUi::Present`, `Magpie::PieUi::OpenMap` (Task 1); the existing file-local `LinkKey(const std::string&, uint8_t&, uint32_t&)` helper; `PieUI::ChatLinks::LINK_MAP` (== `0x04`).
- Produces: nothing new — final user-facing behaviour.

- [ ] **Step 1: Add the include**

Next to the existing `#include "DecoderClient.h"` (line 26), add:

```cpp
#include "DecoderClient.h"           // Magpie::Decoder::Present / Resolve
#include "PieUiClient.h"             // Magpie::PieUi::Present / OpenMap
```

- [ ] **Step 2: Determine "is this a waypoint, and can we offer the map action" once, in the interaction block**

In `DrawRichChip`, find the interaction block that builds `pid` (around line 356, just before `const bool hovering = ...`). Immediately after the `snprintf(pid, ...)` line, add a single computed flag:

```cpp
    char pid[32];
    snprintf(pid, sizeof pid, "chip_ctx_%d", uid);

    // Waypoint chips (LINK_MAP, 0x04) can ask Pie UI to open + pan the world map.
    // Offered only when Pie UI is present (so an absent Pie UI never shows a dead
    // affordance). Works in BOTH warm and cold chip states: OpenMap hands Pie UI
    // the raw chat code, which Pie UI decodes itself — no Decoder Ring required.
    uint8_t wpType = 0; uint32_t wpId = 0;
    const bool offerMap =
        LinkKey(code, wpType, wpId) &&
        wpType == PieUI::ChatLinks::LINK_MAP &&
        Magpie::PieUi::Present();
```

- [ ] **Step 3: Add the left-click handler and the tooltip line**

Replace the existing `hovering` block (lines ~359-377):

```cpp
    const bool hovering = ImGui::IsMouseHoveringRect(rMin, rMax);
    if (hovering) {
        // Tooltip (suppress while the context menu is open to avoid overlap).
        if (!ImGui::IsPopupOpen(pid)) {
            ImGui::BeginTooltip();
            if (st.warm) {
                TooltipWarm(st.rec, code);
            } else {
                ImGui::TextUnformatted(st.label.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted("Decoder Ring not loaded.");
                ImGui::TextUnformatted("Right-click to copy the code or open the wiki.");
            }
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(pid);
        }
    }
```

with this version (adds the waypoint tooltip line in both warm and cold bodies, plus the left-click action):

```cpp
    const bool hovering = ImGui::IsMouseHoveringRect(rMin, rMax);
    if (hovering) {
        // Tooltip (suppress while the context menu is open to avoid overlap).
        if (!ImGui::IsPopupOpen(pid)) {
            ImGui::BeginTooltip();
            if (st.warm) {
                TooltipWarm(st.rec, code);
            } else {
                ImGui::TextUnformatted(st.label.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted("Decoder Ring not loaded.");
                ImGui::TextUnformatted("Right-click to copy the code or open the wiki.");
            }
            // Discoverability for the Pie UI map action (waypoints, Pie UI present).
            if (offerMap) {
                ImGui::Separator();
                ImGui::TextUnformatted("Left-click: open map in Pie UI");
            }
            ImGui::EndTooltip();
        }
        // Left-click a waypoint -> ask Pie UI to open + pan the map.
        if (offerMap && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Magpie::PieUi::OpenMap(code);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(pid);
        }
    }
```

- [ ] **Step 4: Add the context-menu item**

Replace the existing context-menu block (lines ~381-385):

```cpp
    if (ImGui::BeginPopup(pid)) {
        if (ImGui::MenuItem("Copy chat code")) ImGui::SetClipboardText(code.c_str());
        if (ImGui::MenuItem("Open in wiki"))   OpenWiki(code);
        ImGui::EndPopup();
    }
```

with (adds the gated map item alongside the always-present copy/wiki items):

```cpp
    if (ImGui::BeginPopup(pid)) {
        if (ImGui::MenuItem("Copy chat code")) ImGui::SetClipboardText(code.c_str());
        if (ImGui::MenuItem("Open in wiki"))   OpenWiki(code);
        if (offerMap && ImGui::MenuItem("Open map in Pie UI")) Magpie::PieUi::OpenMap(code);
        ImGui::EndPopup();
    }
```

- [ ] **Step 5: Build the DLL — verify it compiles and links**

Run: `cmake --build build -j`
Expected: build succeeds, `build/MagpieMemo.dll` produced, no errors.

- [ ] **Step 6: Build + run the host tests — verify no regression**

Run: `cmake --build build-host -j && ./build-host/magpie_host_tests`
Expected: builds clean and prints `ALL PASS`.

- [ ] **Step 7: Commit**

```bash
git add src/ChipRich.cpp
git commit -m "feat: waypoint chips left-click to open map in Pie UI"
```

---

### Task 4: In-game verification (manual — user-driven)

This addon's functional behaviour can only be confirmed in Guild Wars 2 with Nexus. The DLL is **deployed by the user**, never by the agent. Hand the build to the user and confirm the four scenarios from the spec.

- [ ] **Step 1: Confirm the build artifact exists**

Run: `ls -la build/MagpieMemo.dll`
Expected: a freshly-built DLL is present. Tell the user it is ready to copy into their addons folder (the user deploys it).

- [ ] **Step 2: With Pie UI installed** — open a saved memo containing a waypoint chip, left-click it. Expected: the GW2 world map opens and pans to that waypoint; no freeze/crash. Right-click Copy chat code / Open in wiki still work. The tooltip shows the "Left-click: open map in Pie UI" line and the right-click menu shows "Open map in Pie UI".

- [ ] **Step 3: With Pie UI NOT installed** — the same memo's waypoint chip shows no "open map" tooltip line, no menu item, and left-click does nothing; copy/wiki still work; nothing errors.

- [ ] **Step 4: Late load** — start with Pie UI absent, load it after Magpie Memo, reopen the memo. Expected: the "open map" affordance now appears (READY latch flipped) without a Magpie Memo restart.

- [ ] **Step 5: Update the handover** once the user confirms the in-game checks, recording the new Pie UI integration, then commit the handover.

---

## Notes for the implementer

- **Do not** add `src/PieUiClient.cpp` to `HOST_TEST_SOURCES` — it is non-pure (needs `AddonAPI_t`) and has nothing pure to unit-test, mirroring `DecoderClient.cpp`.
- **Do not** modify any file under a Pie UI / sibling project directory.
- Keep every new in-game string ASCII.
- The left-click handler must be gated by `offerMap` so non-waypoint chips and the Pie-UI-absent case behave exactly as before.
