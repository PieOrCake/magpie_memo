#pragma once
#include <cstdint>

// Native GW2 chat-link chip colours, lifted as exact VALUES from Pie UI's
// chatbox (src/chat/RichLine.cpp: the default link tint + the per-rarity item
// palette). Magpie draws its own chips; only the colour constants are shared.
//
// Returned values are packed IM_COL32 (ABGR) with alpha 255:
//   (0xFF << 24) | (b << 16) | (g << 8) | r
// so they can be passed straight to ImGui draw-list text without conversion.

namespace Magpie {

// Colour for a non-item chip type (waypoint / skill / build / skin / ...).
// Pie UI renders all of these in the same default link blue.
uint32_t ChipColorForType(uint8_t linkType);

// Colour for an item chip given its DecoderRarity value. DR_RarityUnknown (0 —
// unresolved, service absent, or a pre-v2 cache) maps to the Basic colour.
uint32_t ChipColorForItemRarity(uint8_t rarity);

// Combined rule: an item chip is coloured by its rarity; any other type uses its
// type colour. `rarity` is ignored for non-item types.
uint32_t ChipColor(uint8_t linkType, uint8_t rarity);

} // namespace Magpie
