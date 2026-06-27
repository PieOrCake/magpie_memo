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
// mid-session — acceptable, because OpenChatLink() is a fire-and-forget no-op when
// Pie UI is not actually listening (never a crash).
bool Present();

// Ask Pie UI to perform the native action of the chat link in `chatCode` (a full
// "[&...]" string). Raises EV_PIEUI_OPEN_CHATLINK with the C string as payload;
// Pie UI decodes the link's type byte and acts on the game thread:
//   waypoint / PoI       -> open + pan the world map (no auto-travel)
//   wardrobe template    -> open the native Wardrobe Template window
//   build template       -> open the native build-template window
//   item / skin / outfit -> open the native wardrobe preview
// Malformed / unsupported / non-terminated input is ignored silently. Safe to
// call when Pie UI is absent — the raise is then a no-op.
void OpenChatLink(const std::string& chatCode);

}} // namespace Magpie::PieUi
