# Pie UI "Open Map" Integration — Design

**Date:** 2026-06-25
**Status:** Approved (brainstorming)

## Goal

Left-clicking a **waypoint chip** in a saved Magpie Memo note asks the optional
**Pie UI** addon to open and pan the in-game world map to that waypoint. Pie UI
owns the decode and the game call; Magpie Memo only hands over the chip's
`"[&...]"` chat code over the Nexus event bus.

Magpie Memo itself does no map work and no travelling.

## Hard requirement: graceful fallback

Pie UI is an **optional** dependency. Magpie Memo must load and run fully when
Pie UI is absent. With Pie UI missing:

- the waypoint chip keeps its current behaviour (right-click -> Copy chat code /
  Open in wiki),
- the "open map" affordance is simply not offered (no dead button),
- nothing errors.

If Pie UI loads mid-session, the affordance appears without a restart.

## Pie UI contract (authoritative)

Pie UI has already shipped its side. It exposes these Nexus events:

- **`EV_PIEUI_OPEN_MAP`** — *Magpie raises this.* Payload = a null-terminated
  `"[&...]"` chat-code string (`void*` -> C string). Pie UI decodes the PoI id
  and opens + pans the map. It validates strictly: a non-waypoint or malformed
  link is ignored silently. No reply. Nexus delivers events synchronously, so a
  pointer to a local string's `c_str()` is valid for the duration of the raise.
- **`EV_PIE_UI_PING`** — *Magpie raises this on load* to probe for Pie UI.
- **`EV_PIE_UI_READY`** — *Pie UI raises this* on its own load and in reply to a
  ping. *Magpie subscribes* and latches "Pie UI present".

Presence detection is optional for *correctness* (raising `EV_PIEUI_OPEN_MAP`
when Pie UI is absent is a harmless no-op) but required to decide whether to show
the click affordance.

Pie UI has no "unloading" event, so the presence latch can go stale if Pie UI
unloads mid-session. This is **acceptable**: the open action is a fire-and-forget
raise, so a stale latch can at worst show an affordance that becomes a no-op —
never a crash. We do not over-engineer around this.

## Architecture

Mirrors the existing optional-dependency client `src/DecoderClient.{h,cpp}`, but
deliberately *simpler* — Magpie is here the **consumer** of Pie UI's map service,
and that service is fire-and-forget (no data returned, no records to cache).

| Aspect              | DecoderClient                       | PieUiClient                          |
|---------------------|-------------------------------------|--------------------------------------|
| Presence            | Live re-resolve (`GetDecoder()`)    | **Latched** `std::atomic<bool>`      |
| Cache / mutex map   | Yes (resolved records)              | **None**                             |
| UNLOADING handler   | Yes (drops cache)                   | **None** (stale latch accepted)      |
| Vendored header     | `DecoderRingApi.h`                  | **None** — EV constants file-local   |

### New unit: `src/PieUiClient.{h,cpp}` (namespace `Magpie::PieUi`)

```cpp
namespace Magpie { namespace PieUi {
    void Init(AddonAPI_t* api);   // subscribe EV_PIE_UI_READY, then raise EV_PIE_UI_PING
    void Shutdown();              // unsubscribe the SAME fn pointer, null APIDefs
    bool Present();               // latched true once EV_PIE_UI_READY seen this session
    void OpenMap(const std::string& chatCode);  // raise EV_PIEUI_OPEN_MAP with payload
}}
```

File-local implementation state:
- `AddonAPI_t* APIDefs = nullptr;`
- The three `EV_*` string constants, defined on **our** side (we never touch the
  Pie UI project, so there is no header to vendor):
  `EV_PIEUI_OPEN_MAP`, `EV_PIE_UI_PING`, `EV_PIE_UI_READY`.
- `std::atomic<bool> s_present{false};` — the READY handler may fire off the
  render thread.

Behaviour:
- `OnReady(void*)` -> `s_present = true;`
- `Init(api)` -> store `APIDefs`; if null, return. Subscribe
  `EV_PIE_UI_READY` -> `OnReady`, then `Events_Raise(EV_PIE_UI_PING, nullptr)`.
- `Shutdown()` -> if `APIDefs`, `Events_Unsubscribe(EV_PIE_UI_READY, OnReady)`;
  then `APIDefs = nullptr`. (Latch is intentionally left as-is — harmless.)
- `Present()` -> `s_present.load()`.
- `OpenMap(code)` -> guard `APIDefs && APIDefs->Events_Raise && !code.empty()`,
  then `APIDefs->Events_Raise(EV_PIEUI_OPEN_MAP, (void*)code.c_str())`. Safe to
  raise even when `!Present()` (Pie UI just is not listening).

### Wiring: `src/dllmain.cpp`

- `#include "PieUiClient.h"`.
- In `AddonLoad`, next to `Magpie::Decoder::Init(APIDefs);` add
  `Magpie::PieUi::Init(APIDefs);`.
- In `AddonUnload`, alongside the Decoder shutdown, add
  `Magpie::PieUi::Shutdown();` — **before** `APIDefs` is nulled.

### Build: `CMakeLists.txt`

- Add `src/PieUiClient.cpp` to the DLL `SOURCES` list only.
- **Not** added to `HOST_TEST_SOURCES`: the unit is non-pure (depends on
  `AddonAPI_t`) and has nothing pure to unit-test — exactly like
  `DecoderClient.cpp`. The existing host-test suite keeps passing unchanged.

### Click affordance: `src/ChipRich.cpp`, `DrawRichChip(...)`

`#include "PieUiClient.h"`.

The interaction block already manual-hit-tests the chip rect and handles
right-click. The chip's `[&...]` string is the `code` parameter; its link type
comes from the existing `LinkKey(code, linkType, id)` helper
(`linkType == 0x04` is `LINK_MAP`, i.e. a waypoint).

Add, **only for waypoint chips (`linkType == 0x04`) AND only when
`Magpie::PieUi::Present()`** — applied in BOTH warm and cold chip states
(the action works without Decoder Ring, since Pie UI decodes the raw code):

1. **Left-click opens the map** — inside the `hovering` branch, alongside the
   right-click check:
   ```cpp
   if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
       Magpie::PieUi::OpenMap(code);
   ```
2. **Discoverability** — append one ASCII tooltip line,
   `"Left-click: open map in Pie UI"`, to the waypoint tooltip in **both** the
   warm and cold tooltip bodies. Existing Decoder-Ring tooltip text is kept
   intact; the line is only appended.
3. **Context menu** — add a `"Open map in Pie UI"` item, gated the same way, that
   also calls `Magpie::PieUi::OpenMap(code)`.

When Pie UI is **not** present: nothing changes about the chip — no left-click
action, no extra tooltip line, no menu item. Copy-code and wiki keep working with
or without Pie UI and with or without Decoder Ring.

The waypoint link type is computed once from `LinkKey(code, ...)` near the
interaction block (a non-waypoint or unparseable code -> no map affordance).

## Constraints / house rules

- **Never modify the Pie UI project.** Implement only the Magpie Memo side.
- **No hard dependency.** Magpie loads and runs fully with Pie UI absent.
- **All rendered / tooltip text must be ASCII** (Nexus font is ASCII-only) — no
  em-dashes, arrows, or Unicode in any in-game string.
- Match surrounding code style, naming, comment density, and the `Magpie::`
  namespace convention. Reuse the vendored `ChatLinks` codec — do not fork it.

## Verification

1. **Build:** addon DLL compiles + links with `src/PieUiClient.cpp` added to
   `CMakeLists.txt`. Host-test target still builds + passes (unchanged).
2. **In-game, Pie UI installed:** open a saved memo with a waypoint chip,
   left-click -> world map opens and pans to that waypoint; no freeze/crash;
   right-click Copy/Wiki still work.
3. **In-game, Pie UI NOT installed:** the waypoint chip shows no "open map"
   affordance and behaves exactly as before; nothing errors.
4. **Late load:** start with Pie UI absent, load it after Magpie Memo, reopen the
   memo -> the "open map" affordance now appears (READY latch flips).

## Definition of done

- `src/PieUiClient.{h,cpp}` added, wired in `dllmain` load/unload, listed in
  `CMakeLists.txt` (DLL sources only).
- Waypoint chips left-click -> `EV_PIEUI_OPEN_MAP` raised, gated on presence
  (in both warm and cold chip states).
- Full graceful fallback verified (absent + late-load).
- Builds clean; existing chip behaviour unchanged when Pie UI is absent.

## Out of scope

- Any map/decode/travel logic in Magpie Memo (Pie UI owns all of it).
- A Pie UI "unloading" handler / live presence (latch is sufficient; stale latch
  accepted).
- Edit-mode chip clicks (this is the VIEW-mode renderer `DrawRichChip`).
- Modifying the Pie UI project in any way.
