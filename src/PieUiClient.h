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
