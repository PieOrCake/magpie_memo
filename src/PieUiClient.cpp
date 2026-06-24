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
