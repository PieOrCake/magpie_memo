#include "IconCache.h"
#include "IconUrl.h"

#include <mutex>
#include <unordered_map>
#include <string>
#include <cstdio>

namespace Magpie { namespace Icons {

// ── Internal state ────────────────────────────────────────────────────────────

namespace {

AddonAPI_t* APIDefs = nullptr;
// Set true in Shutdown() so a Nexus texture-load callback that lands AFTER the addon
// has begun unloading is ignored rather than touching torn-down state.
bool        s_dead   = false;

enum class LoadState { Loading, Ready, Failed };

struct Entry {
    LoadState   state      = LoadState::Loading;
    Texture_t*  texture    = nullptr;
    std::string identifier; // "MM_ICON_" + hex(key) — kept alive for Nexus
};

std::mutex                                s_mutex;
std::unordered_map<uint32_t, Entry>       s_cache;
// Secondary map: identifier string -> key, for O(1) callback lookup
std::unordered_map<std::string, uint32_t> s_idToKey;

} // anonymous namespace

// ── Callback (loader thread) — defined before KickLoad which passes it ────────

static void OnTextureLoaded(const char* identifier, Texture_t* texture)
{
    if (!identifier) return;

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_dead) return;  // addon is unloading — don't touch the (being-torn-down) cache

    auto it = s_idToKey.find(identifier);
    if (it == s_idToKey.end()) return;

    uint32_t key = it->second;
    auto& entry  = s_cache[key];

    if (texture && texture->Resource)
    {
        entry.texture = texture;
        entry.state   = LoadState::Ready;
    }
    else
    {
        // Failed or texture not GPU-ready — never poison-cache a bad texture
        entry.texture = nullptr;
        entry.state   = LoadState::Failed;
    }
}

// ── Internal helper ───────────────────────────────────────────────────────────

// Insert a Loading entry and fire the async download.
// Must NOT be called while s_mutex is held.
static void KickLoad(uint32_t key, const char* url)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "MM_ICON_%08X", key);
    std::string identifier(buf);

    {
        std::lock_guard<std::mutex> lock(s_mutex);

        // Guard against a race where two callers both observed "not tracked"
        if (s_cache.count(key)) return;

        Entry entry;
        entry.state      = LoadState::Loading;
        entry.texture    = nullptr;
        entry.identifier = identifier;

        s_idToKey[identifier] = key;
        s_cache.emplace(key, std::move(entry));
    }

    // s_mutex NOT held here — safe to call into Nexus
    SplitUrl split = SplitIconUrl(url);
    APIDefs->Textures_LoadFromURL(
        identifier.c_str(),
        split.remote.c_str(),
        split.endpoint.c_str(),
        OnTextureLoaded
    );
}

// ── Public API ────────────────────────────────────────────────────────────────

void Init(AddonAPI_t* api)
{
    APIDefs = api;
}

void Shutdown()
{
    // Mark dead so any in-flight Textures_LoadFromURL callback that fires during/after
    // unload early-returns instead of touching freed state. (The callback address itself
    // is freed by FreeLibrary; Nexus is expected to quiesce loads, but this closes the
    // realistic teardown-race window — parity with DecoderClient's unload discipline.)
    std::lock_guard<std::mutex> lock(s_mutex);
    s_dead  = true;
    APIDefs = nullptr;
    s_cache.clear();
    s_idToKey.clear();
}

void RequestUrl(const char* url)
{
    if (!url || !*url || !APIDefs) return;

    uint32_t key = UrlKey(url);

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_cache.count(key)) return; // already tracked (Loading / Ready / Failed)
    }

    KickLoad(key, url);
}

Texture_t* Get(const char* url)
{
    if (!url || !*url) return nullptr;

    uint32_t key = UrlKey(url);

    {
        std::lock_guard<std::mutex> lock(s_mutex);

        auto it = s_cache.find(key);
        if (it != s_cache.end())
        {
            // Already tracked — return texture only if truly GPU-ready
            if (it->second.state == LoadState::Ready &&
                it->second.texture != nullptr &&
                it->second.texture->Resource != nullptr)
            {
                return it->second.texture;
            }
            return nullptr;
        }
        // Not tracked — fall through (lock released by scope exit)
    }

    // Lazily kick the download; return nullptr for this frame
    if (APIDefs) KickLoad(key, url);
    return nullptr;
}

}} // namespace Magpie::Icons
