// ChipRich.cpp — rich-chip resolution + draw. See ChipRich.h.
//
// Degradation contract (must never crash, must always render):
//   * Decoder Ring absent / Resolve()==false  -> structural label, no icon, no
//     rich tooltip; context menu STILL works (it uses the raw stored code).
//   * Icon not yet downloaded / failed         -> text-only chip; still works.
//   * Record warm                              -> resolved name + icon + rich
//     tooltip (type-specific: item stats/flavour/value/flags + rune-set bonuses,
//     skill desc+facts, waypoint map).
//
// Presence is read live every call via Decoder::Present()/Resolve() — never
// latched. All tooltip/rendered text is ASCII.

#include "ChipRich.h"

#include <windows.h>
#include <shellapi.h>

#include <cfloat>   // FLT_MAX
#include <cstdio>   // snprintf
#include <cstring>  // strlen
#include <string>

#include "ChipResolver.h"            // Magpie::ResolveChip (structural label)
#include "ChipColor.h"               // Magpie::ChipColor (native GW2 chip colours)
#include "DecoderClient.h"           // Magpie::Decoder::Present / Resolve
#include "DecoderRingApi.h"          // DecoderRecord + enums (rarity)
#include "IconCache.h"               // Magpie::Icons::RequestUrl / Get
#include "chat/ChatLinks.h"          // SegmentLine -> (linkType, id)

namespace Magpie {

namespace {

constexpr float kIconGap  = 3.0f;   // gap between the inline icon and the label

// Pull the (linkType, id) correlation key out of a chat code. A single chat
// code segments to one Link segment; if anything is off we return false and the
// caller falls back to a pure-structural chip (label only, no Decoder query).
bool LinkKey(const std::string& code, uint8_t& linkType, uint32_t& id)
{
    for (const auto& seg : PieUI::ChatLinks::SegmentLine(code)) {
        if (seg.kind == PieUI::ChatLinks::SegmentKind::Link) {
            linkType = (uint8_t)seg.linkType;
            id = seg.primaryId;
            return true;
        }
    }
    return false;
}

// Resolve the chip to its display state for this frame.
// label: what to draw (bracketed). warm: true if a Decoder record was copied.
// iconUrl: from the record when warm (may be empty). color: structural tint.
struct ChipState {
    std::string   label;
    uint32_t      color = 0xFFA0A0A0;
    bool          warm  = false;
    DecoderRecord rec{};       // valid only when warm
};

ChipState Resolve(const std::string& code)
{
    ChipState st;
    // Structural label is the offline fallback (works with no Decoder Ring).
    const ChipView structural = Magpie::ResolveChip(code);
    st.label = structural.label.empty() ? "[link]" : structural.label;

    uint8_t linkType = 0; uint32_t id = 0;
    const bool haveKey = LinkKey(code, linkType, id);

    // Item rarity is only known from a warm Decoder record; default Unknown
    // (-> Basic colour) so cold/absent items still render in a sensible colour.
    uint8_t rarity = DR_RarityUnknown;

    if (haveKey && Magpie::Decoder::Present()) {
        DecoderRecord rec{};
        if (Magpie::Decoder::Resolve(linkType, id, code.c_str(), rec) && rec.name[0] != '\0') {
            st.warm = true;
            st.rec  = rec;
            std::string name(rec.name);
            st.label = "[" + name + "]";   // bracket to match the structural style
            if (linkType == PieUI::ChatLinks::LINK_ITEM) rarity = rec.rarity;
        }
    }

    // Colour is a pure function of (type, rarity): items by rarity, everything
    // else the native link blue — recomputed from the current record state.
    st.color = ChipColor(linkType, rarity);
    return st;
}

// Percent-encode everything that is not an RFC-3986 unreserved char. The chat
// code contains '[', '&', ']' which MUST be encoded for the wiki search URL.
std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

void OpenWiki(const std::string& code)
{
    const std::string url = "https://wiki.guildwars2.com/wiki/?search=" + UrlEncode(code);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Format a copper amount as "G g S s C c" (ASCII), dropping leading zero tiers.
void AppendCoin(std::string& out, int32_t copper)
{
    if (copper < 0) copper = 0;
    const int32_t g = copper / 10000;
    const int32_t s = (copper / 100) % 100;
    const int32_t c = copper % 100;
    char buf[64];
    if (g > 0)      snprintf(buf, sizeof buf, "%dg %ds %dc", g, s, c);
    else if (s > 0) snprintf(buf, sizeof buf, "%ds %dc", s, c);
    else            snprintf(buf, sizeof buf, "%dc", c);
    out += buf;
}

const char* BoundText(uint8_t bound)
{
    switch (bound) {
        case DB_AccountOnAcquire: return "Account Bound on Acquire";
        case DB_SoulOnAcquire:    return "Soulbound on Acquire";
        case DB_AccountOnUse:     return "Account Bound on Use";
        case DB_SoulOnUse:        return "Soulbound on Use";
        default:                  return nullptr;
    }
}

// Emit up to 16 pre-formatted fact lines (DR clamps factCount, we clamp again).
void EmitFacts(const DecoderRecord& rec)
{
    const int n = rec.factCount > 16 ? 16 : rec.factCount;
    for (int i = 0; i < n; ++i) {
        if (rec.facts[i].text[0]) ImGui::TextUnformatted(rec.facts[i].text);
    }
}

// Build + emit the tooltip body for a warm record (type-specific). `code` is the
// chip's chat code, used only to fetch upgrade (rune/sigil) records for items.
void TooltipWarm(const DecoderRecord& rec, const std::string& code)
{
    const char* nm = rec.name[0] ? rec.name : "(unnamed)";

    // Items get a rarity-coloured header so the tooltip matches the chip colour.
    if (rec.linkType == 0x02) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ChipColorForItemRarity(rec.rarity)),
                           "%s", nm);
    } else {
        ImGui::TextUnformatted(nm);
    }

    switch (rec.linkType) {
        case 0x02: {  // item
            // v3+: pre-formatted stat / meta lines (Defense, Weapon Strength,
            // attributes, infusion slots, weight, required level). Empty against
            // an older DR, so the tooltip is simply unchanged there.
            const int nFacts = rec.factCount > 16 ? 16 : rec.factCount;
            if (nFacts > 0) {
                ImGui::Separator();
                EmitFacts(rec);
            }
            // Vendor value + bind/trade flags. DR deliberately keeps these OUT of
            // facts[], so there is no duplication with the stat lines above.
            ImGui::Separator();
            std::string v = "Vendor: ";
            AppendCoin(v, rec.vendorValue);
            ImGui::TextUnformatted(v.c_str());
            if (const char* b = BoundText(rec.bound)) ImGui::TextUnformatted(b);
            ImGui::TextUnformatted(rec.tradeable ? "Tradeable on the Trading Post"
                                                 : "Not tradeable");
            if (rec.noSell) ImGui::TextUnformatted("Cannot be sold to a vendor");

            // Rune / sigil set bonuses live on the UPGRADE item's own record (DR
            // keeps records flat), so decode the chip's upgrade ids and resolve
            // each as a normal item link. Degrades cleanly: a not-yet-warm upgrade
            // is skipped and fills on a later frame via the RESOLVED event.
            PieUI::ChatLinks::DecodedItemLink di;
            if (PieUI::ChatLinks::DecodeItem(code, di)) {
                const uint32_t ups[2] = { di.upgrade1_id, di.upgrade2_id };
                bool header = false;
                for (uint32_t uid : ups) {
                    if (uid == 0) continue;
                    DecoderRecord up{};
                    if (Magpie::Decoder::Resolve(PieUI::ChatLinks::LINK_ITEM, uid, nullptr, up)) {
                        if (!header) {
                            ImGui::Separator();
                            ImGui::TextUnformatted("Upgrades:");
                            header = true;
                        }
                        if (up.name[0]) ImGui::TextUnformatted(up.name);
                        EmitFacts(up);
                    }
                }
            }

            // Flavour text reads best at the very foot of the tooltip.
            if (rec.description[0]) {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(rec.description);
                ImGui::PopTextWrapPos();
            }
            break;
        }
        case 0x06: {  // skill
            if (rec.description[0]) {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(rec.description);
                ImGui::PopTextWrapPos();
            }
            if (rec.factCount > 0) {
                ImGui::Separator();
                EmitFacts(rec);
            }
            break;
        }
        case 0x04: {  // waypoint / PoI
            if (rec.mapName[0]) {
                ImGui::Separator();
                std::string m = "Map: ";
                m += rec.mapName;
                ImGui::TextUnformatted(m.c_str());
            }
            break;
        }
        default:
            break;  // skin / build / other: name only
    }
}

} // namespace

std::string ChipLabel(const std::string& code)
{
    return Resolve(code).label;
}

uint32_t ChipDisplayColor(const std::string& code)
{
    return Resolve(code).color;
}

// Width = optional inline icon (square fontSize) + gap + coloured label text.
// No box/padding — the chip is just coloured inline text (matching the chatbox).
// Re-derives the same ChipState as the draw so the two agree.
static float ComputeWidth(ImFont* font, float fontSize, const ChipState& st)
{
    float w = 0.0f;
    // Icon is shown only when warm AND the texture is ready; the wrap pre-check
    // may briefly disagree with the draw on the single frame an icon finishes
    // loading — harmless (only affects a wrap decision, never correctness).
    if (st.warm && st.rec.iconUrl[0]) {
        Magpie::Icons::RequestUrl(st.rec.iconUrl);
        if (Magpie::Icons::Get(st.rec.iconUrl) != nullptr) w += fontSize + kIconGap;
    }
    w += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, st.label.c_str()).x;
    return w;
}

float MeasureRichChip(ImFont* font, float fontSize, const std::string& code)
{
    return ComputeWidth(font, fontSize, Resolve(code));
}

float DrawRichChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                   const std::string& code, int uid)
{
    const ChipState st = Resolve(code);
    const float width = ComputeWidth(font, fontSize, st);

    // No box / border / background — the chip is coloured inline text. The rect
    // below is used only for hover/right-click hit-testing, never drawn.
    const float padY = 1.0f;
    const ImVec2 rMin(pos.x, pos.y - padY);
    const ImVec2 rMax(pos.x + width, pos.y + fontSize + padY);

    float penX = pos.x;

    // Optional inline icon (square, fontSize x fontSize) — only when warm + ready.
    if (st.warm && st.rec.iconUrl[0]) {
        Texture_t* tex = Magpie::Icons::Get(st.rec.iconUrl);  // never null Resource
        if (tex != nullptr) {
            const ImVec2 iMin(penX, pos.y);
            const ImVec2 iMax(penX + fontSize, pos.y + fontSize);
            dl->AddImage((ImTextureID)tex->Resource, iMin, iMax);
            penX += fontSize + kIconGap;
        }
    }

    // Label as coloured inline text — colour conveys type (and item rarity).
    dl->AddText(font, fontSize, ImVec2(penX, pos.y), st.color, st.label.c_str());

    // ── Interaction: manual hit-test against the chip rect ──────────────────
    // We are inside the view child window's draw, so OpenPopup/BeginPopup hash
    // their id against THIS window (matches where the body is drawn). BeginPopup
    // runs every frame (not only when hovered) so an open popup keeps showing.
    char pid[32];
    snprintf(pid, sizeof pid, "chip_ctx_%d", uid);

    const bool hovering = ImGui::IsMouseHoveringRect(rMin, rMax);
    if (hovering) {
        // Tooltip (suppress while the context menu is open to avoid overlap).
        if (!ImGui::IsPopupOpen(pid)) {
            ImGui::BeginTooltip();
            if (st.warm) {
                TooltipWarm(st.rec, code);
            } else {
                ImGui::TextUnformatted(st.label.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted("Decoder Ring not loaded.");
                ImGui::TextUnformatted("Right-click to copy the code or open the wiki.");
            }
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(pid);
        }
    }

    // Context menu — both actions use the raw stored code, so they work with NO
    // Decoder Ring present. Begun every frame in this window scope.
    if (ImGui::BeginPopup(pid)) {
        if (ImGui::MenuItem("Copy chat code")) ImGui::SetClipboardText(code.c_str());
        if (ImGui::MenuItem("Open in wiki"))   OpenWiki(code);
        ImGui::EndPopup();
    }

    return width;
}

} // namespace Magpie
