#include "ChipCodec.h"

namespace Magpie {

// ---- small UTF-8 <-> codepoint helpers --------------------------------------------------------
void AppendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

std::vector<uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = (unsigned char)s[i];
        uint32_t cp; int n;
        if (b < 0x80)       { cp = b; n = 1; }
        else if (b < 0xE0)  { cp = b & 0x1F; n = 2; }
        else if (b < 0xF0)  { cp = b & 0x0F; n = 3; }
        else                { cp = b & 0x07; n = 4; }
        if (i + (size_t)n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += n;
    }
    return out;
}

// ---- pure parse / serialize -------------------------------------------------------------------
std::vector<ParsedCell> ParseToCells(const std::string& utf8) {
    using namespace PieUI::ChatLinks;
    std::vector<ParsedCell> cells;
    for (const auto& seg : SegmentLine(utf8)) {
        if (seg.kind == SegmentKind::Link || seg.kind == SegmentKind::Ae2) {
            ParsedCell c; c.isChip = true; c.code = seg.raw;
            cells.push_back(std::move(c));
        } else {
            // Plain / Url: emit the source slice (raw), so the round-trip is byte-exact;
            // each codepoint (including '\n') becomes its own text cell.
            const std::string& t = seg.raw.empty() ? seg.display : seg.raw;
            for (uint32_t cp : DecodeUtf8(t)) {
                ParsedCell c; c.isChip = false; c.cp = cp;
                cells.push_back(std::move(c));
            }
        }
    }
    return cells;
}

std::string CellsToText(const std::vector<ParsedCell>& cells) {
    std::string out;
    for (const auto& c : cells) {
        if (c.isChip) out += c.code;
        else          AppendUtf8(out, c.cp);
    }
    return out;
}

} // namespace Magpie
