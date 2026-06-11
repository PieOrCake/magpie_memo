#pragma once
// ChipRich — rich-chip resolution + draw (NON-PURE: ImGui/Nexus/Decoder/Icons).
//
// Task 13: turns a stored "[&...]" chat code into a rendered token that, when
// Decoder Ring is present and warm, shows the resolved name + icon + a tooltip;
// and always (with or without Decoder Ring) offers a right-click context menu
// (Copy chat code / Open in wiki). When Decoder Ring is absent or the record is
// not yet warm, the chip degrades to the structural label from ResolveChip and
// stays fully functional. The icon is optional — text-only until/if it loads.
//
// All rendered + tooltip text is ASCII.

#include "imgui.h"
#include <string>
#include <cstdint>

namespace Magpie {

// Resolved display label for a chip: Decoder Ring's resolved name if warm, else
// the structural label from ResolveChip. Bracketed to match the structural
// style ("[Copper Ore]" / "[Item]"). Never returns empty.
// Used by BOTH edit-mode cells and view-mode chips.
std::string ChipLabel(const std::string& code);

// Native GW2 chip colour for a code: item rarity colour when a warm Decoder
// record carries it, else the per-type colour (waypoint/skill/build/... blue;
// items default to Basic until resolved). Packed IM_COL32. Used by edit-mode
// cells so they match the view-mode chip colours.
uint32_t ChipDisplayColor(const std::string& code);

// Width (advance) a rich chip will consume at the given font/size. Must match
// what DrawRichChip produces so the renderer's wrap check stays consistent.
float MeasureRichChip(ImFont* font, float fontSize, const std::string& code);

// Draw a rich chip token at screen `pos`; returns its advance width (== the
// value MeasureRichChip returned for the same args). Draws an optional icon +
// the label over a tinted rounded background, and registers a hover tooltip and
// a right-click context menu. `uid` disambiguates this chip's popup id.
float DrawRichChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                   const std::string& code, int uid);

} // namespace Magpie
