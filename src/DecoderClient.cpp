// ============================================================================
// DecoderClient — the ONLY place that talks to the optional Decoder Ring
// provider. Implements Decoder Ring's consumer LIFETIME CONTRACT exactly
// (docs/API.md §2,3,4,6,7) — the part that prevents a game crash when Decoder
// Ring unloads at runtime.
//
// Lifetime rules realised here:
//   1. NEVER cache the DecoderRingApi*. Re-resolve via GetDecoder() (a cheap
//      DataLink_Get + apiVersion check) immediately before EVERY use.
//   2. Subscribe READY (appearance) + UNLOADING (disappearance). On UNLOADING
//      drop the whole cache + pending state. Presence is derived live from
//      GetDecoder(), never latched.
//   3. On load: subscribe events, then raise PING (catches a Decoder that
//      loaded first). On unload: unsubscribe the SAME function pointers.
//   4. Service absent is a NORMAL state — GetDecoder() == nullptr → caller
//      degrades to structural fallback, never crashes.
//
// The RESOLVED event handler may fire on the service's BACKGROUND thread while
// Resolve() runs on the render thread, so ALL cache/pending access is guarded
// by a single mutex.
// ============================================================================
#include "DecoderClient.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Magpie { namespace Decoder {

namespace {

AddonAPI_t* APIDefs = nullptr;

// Lowest Decoder Ring ABI we still understand. DR's contract is additive and
// layout-stable across bumps (v2, v3 and v4 share an identical struct layout —
// v3 gave existing description[]/facts[] new meaning for items, v4 added recipe
// links reusing those same fields), so we accept any service at-or-above this
// minimum and read only the fields we know,
// treating empty fields as "absent". This keeps Magpie working across future
// additive DR releases instead of going dark the moment apiVersion ticks up.
// RAISE THIS (and re-vendor the header) only if DR announces a LAYOUT-breaking
// change. The host tests still pin the vendored header to the current version.
constexpr uint32_t DECODER_RING_MIN_SUPPORTED = 2u;

std::mutex                                g_mutex;
std::unordered_map<uint64_t, DecoderRecord> g_cache;   // key -> resolved record
std::unordered_set<uint64_t>              g_pending;  // keys with a fetch in flight

// Correlation key = (linkType, id). Matches the service's own correlation tuple.
inline uint64_t Key(uint8_t linkType, uint32_t id) {
    return (static_cast<uint64_t>(linkType) << 32) | static_cast<uint64_t>(id);
}

// Re-resolve the service pointer per call. NEVER store the result in a
// long-lived variable — the DataLink block survives an unload but its function
// pointers are zeroed, so a stale pointer crashes the game.
DecoderRingApi* GetDecoder() {
    if (!APIDefs) return nullptr;
    auto* api = static_cast<DecoderRingApi*>(APIDefs->DataLink_Get(DECODER_RING_DATALINK));
    return (api && api->apiVersion >= DECODER_RING_MIN_SUPPORTED) ? api : nullptr;
}

// ── Event handlers (void(*)(void*) == EVENT_CONSUME) ───────────────────────

// A background resolution landed. Payload is a DecoderRecord* valid ONLY during
// this call — copy out immediately. May run on the service's background thread.
void OnResolved(void* payload) {
    if (!payload) return;
    auto* rec = static_cast<const DecoderRecord*>(payload);
    if (rec->schemaVersion < DECODER_RING_MIN_SUPPORTED) return;

    const uint64_t k = Key(rec->linkType, rec->id);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache[k] = *rec;       // copy out of the volatile payload
    g_pending.erase(k);
}

// Service (re)announced readiness. Presence is derived live from GetDecoder(),
// so nothing to latch here — kept as a no-op on purpose.
void OnReady(void*) {
    // intentionally empty: Present() is the live source of truth.
}

// Service is unloading. Drop EVERYTHING — its records are about to point at
// dead code. GetDecoder() will return null hereafter, so Present() flips false
// automatically.
void OnUnloading(void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
    g_pending.clear();
}

} // namespace

void Init(AddonAPI_t* api) {
    APIDefs = api;
    if (!APIDefs) return;

    // Subscribe appearance + disappearance + completion before pinging.
    APIDefs->Events_Subscribe(EV_DECODER_RING_READY,     OnReady);
    APIDefs->Events_Subscribe(EV_DECODER_RING_UNLOADING, OnUnloading);
    APIDefs->Events_Subscribe(EV_DECODER_RING_RESOLVED,  OnResolved);

    // Prompt a READY reply in case Decoder Ring loaded before us.
    APIDefs->Events_Raise(EV_DECODER_RING_PING, nullptr);
}

void Shutdown() {
    if (APIDefs) {
        // Unsubscribe the SAME function pointers we subscribed.
        APIDefs->Events_Unsubscribe(EV_DECODER_RING_READY,     OnReady);
        APIDefs->Events_Unsubscribe(EV_DECODER_RING_UNLOADING, OnUnloading);
        APIDefs->Events_Unsubscribe(EV_DECODER_RING_RESOLVED,  OnResolved);
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cache.clear();
        g_pending.clear();
    }
    APIDefs = nullptr;
}

bool Present() {
    return GetDecoder() != nullptr;
}

bool Resolve(uint8_t linkType, uint32_t id, const char* chatCode, DecoderRecord& out) {
    const uint64_t k = Key(linkType, id);

    // 1. Warm cache hit — serve it without touching the service.
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_cache.find(k);
        if (it != g_cache.end() && it->second.status == DR_Resolved) {
            out = it->second;
            return true;
        }
    }

    // 2. Re-validate the service pointer per call. Absent → caller falls back.
    auto* dec = GetDecoder();
    if (!dec) return false;

    // 3. Warm query — never blocks on the network.
    DecoderRecord rec{};
    DecoderStatus st = dec->Resolve(linkType, id, chatCode, &rec);

    switch (st) {
    case DR_Resolved:
        if (rec.schemaVersion < DECODER_RING_MIN_SUPPORTED) return false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[k] = rec;
            g_pending.erase(k);
        }
        out = rec;
        return true;

    case DR_NotReady:
        // Fetch kicked; the RESOLVED event will fill the cache later.
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_pending.insert(k);
        }
        return false;

    case DR_Failed:
    default:
        // Cooldown — retryable on a later call. Do not cache as resolved.
        return false;
    }
}

}} // namespace Magpie::Decoder
