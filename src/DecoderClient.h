#pragma once
#include "nexus/Nexus.h"
#include "DecoderRingApi.h"

namespace Magpie { namespace Decoder {

// Subscribe READY/UNLOADING/RESOLVED, then raise PING. Call from AddonLoad
// after APIDefs is set.
void Init(AddonAPI_t* api);

// Unsubscribe all events and clear the cache. Call from AddonUnload before
// APIDefs is nulled.
void Shutdown();

// Live presence: GetDecoder() != nullptr, re-evaluated on every call.
// NEVER latched — the service can appear/disappear at any time.
bool Present();

// Warm-or-event resolve. If a resolved record for (linkType,id) is warm (in our
// cache or returned warm by the service), copy it into `out` and return true.
// Otherwise kick a fetch (the service does this on a NotReady), record the
// pending (linkType,id), and return false (caller renders the structural
// fallback until the RESOLVED event fills the cache).
// `chatCode` is the full "[&...]" string; REQUIRED for build links (0x0D),
// harmless otherwise.
bool Resolve(uint8_t linkType, uint32_t id, const char* chatCode, DecoderRecord& out);

}} // namespace Magpie::Decoder
