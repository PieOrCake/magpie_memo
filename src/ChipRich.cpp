// ChipRich.cpp — rich-chip resolution + draw. See ChipRich.h.
//
// Degradation contract (must never crash, must always render):
//   * Decoder Ring absent / Resolve()==false  -> structural label, no icon, no
//     rich tooltip; context menu STILL works (it uses the raw stored code).
//   * Icon not yet downloaded / failed         -> text-only chip; still works.
//   * Record warm                              -> resolved name + icon + rich
//     tooltip (type-specific: item value/flags, skill desc+facts, waypoint map).
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

#include "ChipResolver.h"            // Magpie::ResolveChip (structural)
#include "DecoderClient.h"           // Magpie::Decoder::Present / Resolve
#include "DecoderRingApi.h"          // DecoderRecord + enums
#include "IconCache.h"               // Magpie::Icons::RequestUrl / Get
#include "chat/ChatLinks.h"          // SegmentLine -> (linkType, id)

namespace Magpie {

namespace {

constexpr float kPadX     = 4.0f;   // horizontal padding inside the chip bg
constexpr float kIconGap  = 3.0f;   // gap between icon and label

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
    const ChipView structural = Magpie::ResolveChip(code);
    st.color = structural.color;
    st.label = structural.label.empty() ? "[link]" : structural.label;

    uint8_t linkType = 0; uint32_t id = 0;
    if (LinkKey(code, linkType, id) && Magpie::Decoder::Present()) {
        DecoderRecord rec{};
        if (Magpie::Decoder::Resolve(linkType, id, code.c_str(), rec) && rec.name[0] != '\0') {
            st.warm  = true;
            st.rec   = rec;
            // Bracket the resolved name to match the structural style.
            std::string name(rec.name);
            st.label = "[" + name + "]";
        }
    }
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

// Build + emit the tooltip body for a warm record (type-specific).
void TooltipWarm(const DecoderRecord& rec)
{
    ImGui::TextUnformatted(rec.name[0] ? rec.name : "(unnamed)");

    switch (rec.linkType) {
        case 0x02: {  // item
            ImGui::Separator();
            std::string v = "Vendor: ";
            AppendCoin(v, rec.vendorValue);
            ImGui::TextUnformatted(v.c_str());
            if (const char* b = BoundText(rec.bound)) ImGui::TextUnformatted(b);
            ImGui::TextUnformatted(rec.tradeable ? "Tradeable on the Trading Post"
                                                 : "Not tradeable");
            if (rec.noSell) ImGui::TextUnformatted("Cannot be sold to a vendor");
            break;
        }
        case 0x06: {  // skill
            if (rec.description[0]) {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(rec.description);
                ImGui::PopTextWrapPos();
            }
            const int n = rec.factCount > 16 ? 16 : rec.factCount;
            if (n > 0) {
                ImGui::Separator();
                for (int i = 0; i < n; ++i) {
                    if (rec.facts[i].text[0]) ImGui::TextUnformatted(rec.facts[i].text);
                }
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

// Width = optional icon (square fontSize) + gap + label text + 2*padX.
// Re-derives the same ChipState as the draw so the two agree.
static float ComputeWidth(ImFont* font, float fontSize, const ChipState& st)
{
    float w = kPadX * 2.0f;
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

    const float padY = 1.0f;
    const ImVec2 rMin(pos.x, pos.y - padY);
    const ImVec2 rMax(pos.x + width, pos.y + fontSize + padY);

    // Background: faint tint of the structural colour (drop alpha to ~0x40).
    const ImU32 bgCol = (st.color & 0x00FFFFFF) | 0x40000000;
    dl->AddRectFilled(rMin, rMax, bgCol, 3.0f);

    float penX = pos.x + kPadX;

    // Optional icon (square, fontSize x fontSize) — only when warm + ready.
    if (st.warm && st.rec.iconUrl[0]) {
        Texture_t* tex = Magpie::Icons::Get(st.rec.iconUrl);  // never null Resource
        if (tex != nullptr) {
            const ImVec2 iMin(penX, pos.y);
            const ImVec2 iMax(penX + fontSize, pos.y + fontSize);
            dl->AddImage((ImTextureID)tex->Resource, iMin, iMax);
            penX += fontSize + kIconGap;
        }
    }

    // Label text in the structural colour (opaque).
    const ImU32 labelCol = st.color | 0xFF000000;
    dl->AddText(font, fontSize, ImVec2(penX, pos.y), labelCol, st.label.c_str());

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
                TooltipWarm(st.rec);
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
