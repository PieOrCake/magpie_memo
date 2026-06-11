#include "IconUrl.h"

namespace Magpie { namespace Icons {

uint32_t UrlKey(const char* url)
{
    // FNV-1a 32-bit
    uint32_t hash = 2166136261u;
    if (!url) return hash;
    for (const char* p = url; *p; ++p)
    {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;
    }
    return hash;
}

SplitUrl SplitIconUrl(const std::string& url)
{
    // Find "://"
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
    {
        // Malformed: put everything in remote, endpoint = "/"
        return { url, "/" };
    }

    // Host starts after "://"
    auto hostStart = schemeEnd + 3;

    // Find the first '/' after the host
    auto pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos)
    {
        // No path component
        return { url, "/" };
    }

    return { url.substr(0, pathStart), url.substr(pathStart) };
}

}} // namespace Magpie::Icons
