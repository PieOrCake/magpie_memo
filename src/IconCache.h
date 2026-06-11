#pragma once
#include "nexus/Nexus.h"

namespace Magpie { namespace Icons {

// Initialise with the Nexus API pointer. Must be called before RequestUrl/Get.
void Init(AddonAPI_t* api);

// Mark the cache dead on addon unload: late Nexus texture-load callbacks become no-ops.
// Call from AddonUnload before APIDefs is invalidated.
void Shutdown();

// Kick an async download+upload for this URL if not already tracked.
void RequestUrl(const char* url);

// Return the GPU-ready texture for this URL, or nullptr if still loading / failed.
// Lazily calls RequestUrl if the URL has not been seen before.
// NEVER returns a texture whose Resource is null.
Texture_t* Get(const char* url);

}} // namespace Magpie::Icons
