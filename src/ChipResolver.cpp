#include "ChipResolver.h"
#include "chat/ChatLinks.h"
#include "chat/SpecData.h"

// IM_COL32 layout: packed as (a<<24)|(b<<16)|(g<<8)|r
// All colours are chosen for readability on a dark UI theme.

namespace Magpie {

// Per-type colour palette — named constants for clarity.
// Format: IM_COL32(r, g, b, 255) == (0xFF << 24) | (b << 16) | (g << 8) | r
static constexpr uint32_t kColorItem      = 0xFF'C8'8C'4A; // warm amber  — items
static constexpr uint32_t kColorWaypoint  = 0xFF'6A'B8'E8; // sky blue    — waypoints / map
static constexpr uint32_t kColorSkill     = 0xFF'6A'D8'7A; // soft green  — skills
static constexpr uint32_t kColorTrait     = 0xFF'4A'B8'6A; // muted green — traits
static constexpr uint32_t kColorSkin      = 0xFF'C8'8A'D8; // lavender    — skins / outfits
static constexpr uint32_t kColorBuild     = 0xFF'E8'78'78; // soft red    — builds
static constexpr uint32_t kColorRecipe    = 0xFF'78'C8'C8; // teal        — recipes
static constexpr uint32_t kColorAchiev    = 0xFF'E8'C8'50; // gold        — achievements
static constexpr uint32_t kColorWardrobe  = 0xFF'D8'78'C8; // pink        — wardrobe
static constexpr uint32_t kColorWvW       = 0xFF'E8'88'50; // orange      — WvW
static constexpr uint32_t kColorDefault   = 0xFF'A0'A0'A0; // grey        — unknown/other

// Select the colour for a given link type.
static uint32_t ColorForType(PieUI::ChatLinks::ChatLinkType type)
{
    using namespace PieUI::ChatLinks;
    switch (type) {
        case LINK_ITEM:          return kColorItem;
        case LINK_MAP:           return kColorWaypoint;
        case LINK_SKILL:         return kColorSkill;
        case LINK_TRAIT:         return kColorTrait;
        case LINK_SKIN:
        case LINK_OUTFIT:        return kColorSkin;
        case LINK_BUILD:         return kColorBuild;
        case LINK_RECIPE:        return kColorRecipe;
        case LINK_ACHIEVEMENT:   return kColorAchiev;
        case LINK_WARDROBE_TMPL: return kColorWardrobe;
        case LINK_WVW_OBJECTIVE: return kColorWvW;
        default:                 return kColorDefault;
    }
}

// Derive an offline build label from the decoded link.
// Scans all three spec slots for an elite-spec name; falls back to core profession;
// falls back to "[Build]" if everything is unknown.
static std::string BuildLabel(const std::string& chatCode)
{
    using namespace PieUI::ChatLinks;
    DecodedBuildLink out;
    if (!DecodeBuild(chatCode, out))
        return "[Build]";

    // Try each spec slot for a recognised elite-spec name.
    for (int i = 0; i < 3; ++i) {
        const char* es = PieUI::SpecData::GetEliteSpecName(out.specs[i].spec_id);
        if (es && es[0])
            return std::string("[") + es + " Build]";
    }

    // Fall back to core profession name.
    const char* prof = PieUI::SpecData::GetProfessionName(out.profession);
    if (prof && prof[0])
        return std::string("[") + prof + " Build]";

    return "[Build]";
}

ChipView ResolveChip(const std::string& chatCode)
{
    using namespace PieUI::ChatLinks;

    ChipView view;
    ChatLinkType type = DetectType(chatCode);
    view.color = ColorForType(type);

    if (type == LINK_NONE) {
        view.label = "[link]";
        return view;
    }

    if (type == LINK_BUILD) {
        view.label = BuildLabel(chatCode);
        return view;
    }

    view.label = LinkTypeLabel(type);
    return view;
}

} // namespace Magpie
