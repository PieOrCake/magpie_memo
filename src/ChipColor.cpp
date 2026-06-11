#include "ChipColor.h"
#include "chat/ChatLinks.h"   // PieUI::ChatLinks::LINK_ITEM
#include "DecoderRingApi.h"   // DecoderRarity (DR_*)

namespace Magpie {

// IM_COL32(r, g, b, 255) packed (ABGR, alpha 255).
static constexpr uint32_t Pack(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xFFu) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

// ── Pie UI chatbox colour values (RichLine.cpp) ───────────────────────────────
// Default link tint — waypoints, skills, builds, skins all use this blue.
//   RichLineResolveChip: IM_COL32(120, 200, 255, 255)
static constexpr uint32_t kLinkBlue = Pack(120, 200, 255);

// Per-rarity item palette — RarityColor() ImVec4s converted to 0-255 (ImGui rounding).
//   Junk (0.67,0.67,0.67)  Basic (1,1,1)         Fine (0.38,0.64,0.85)
//   Masterwork (0.12,0.84,0) Rare (1,0.84,0)      Exotic (1,0.67,0)
//   Ascended (0.98,0.24,0.55) Legendary (0.64,0.21,0.93)
static constexpr uint32_t kRarJunk       = Pack(171, 171, 171);
static constexpr uint32_t kRarBasic      = Pack(255, 255, 255);
static constexpr uint32_t kRarFine       = Pack( 97, 163, 217);
static constexpr uint32_t kRarMasterwork = Pack( 31, 214,   0);
static constexpr uint32_t kRarRare       = Pack(255, 214,   0);
static constexpr uint32_t kRarExotic     = Pack(255, 171,   0);
static constexpr uint32_t kRarAscended   = Pack(250,  61, 140);
static constexpr uint32_t kRarLegendary  = Pack(163,  54, 237);

uint32_t ChipColorForItemRarity(uint8_t rarity) {
    switch (rarity) {
        case DR_Junk:          return kRarJunk;
        case DR_Basic:         return kRarBasic;
        case DR_Fine:          return kRarFine;
        case DR_Masterwork:    return kRarMasterwork;
        case DR_Rare:          return kRarRare;
        case DR_Exotic:        return kRarExotic;
        case DR_Ascended:      return kRarAscended;
        case DR_Legendary:     return kRarLegendary;
        case DR_RarityUnknown:
        default:               return kRarBasic;   // unresolved / absent -> Basic (white)
    }
}

uint32_t ChipColorForType(uint8_t /*linkType*/) {
    // Pie UI colours every non-item chip type with the one default link blue.
    return kLinkBlue;
}

uint32_t ChipColor(uint8_t linkType, uint8_t rarity) {
    if (linkType == PieUI::ChatLinks::LINK_ITEM)
        return ChipColorForItemRarity(rarity);
    return ChipColorForType(linkType);
}

} // namespace Magpie
