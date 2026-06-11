#pragma once
#include <string>
#include <cstdint>

namespace Magpie { namespace Icons {

// FNV-1a 32-bit hash of the URL (stable key for the cache / texture identifier).
uint32_t UrlKey(const char* url);

// Split a full URL into { remote = "scheme://host", endpoint = "/path..." }.
// If there is no path, endpoint = "/". If the input is malformed (no "://"),
// put the whole thing in remote and set endpoint = "/". Never crashes on empty/null.
struct SplitUrl { std::string remote; std::string endpoint; };
SplitUrl SplitIconUrl(const std::string& url);

}} // namespace Magpie::Icons
