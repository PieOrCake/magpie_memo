// MarkdownRender.cpp — manual draw-list layout of parsed markdown for VIEW mode.
//
// We do our own word-wrapping inline layout (rather than ImGui::TextWrapped)
// because a single line mixes per-span fonts and styles, and because chips
// will later need custom positioning (icon + name token). A "pen" walks
// left-to-right in screen space; words wrap when they would overrun wrapWidth.
//
// Faux styling (Nexus has no bold/italic font):
//   * Bold   -> draw the word twice, offset by 1px horizontally (thickening).
//   * Italic -> draw once, then shear every emitted vertex horizontally based
//               on its distance above the glyph baseline (slants the tops).
//
// All rendered text is ASCII.

#include "MarkdownRender.h"

#include <cfloat>   // FLT_MAX
#include <string>
#include <vector>

#include "imgui.h"

namespace Magpie {

namespace {

// Hanging-indent width for bullets, in "- " worth of pixels (measured live).
// Italic shear factor: ~0.22 slants glyph tops to the right.
constexpr float kItalicShear = 0.22f;
// Small leading added to each line's height.
constexpr float kLeading = 2.0f;

// Split a span's display text into words, each carrying a single trailing
// space (so re-joined spacing is consistent). The final word keeps no
// trailing space only if the source had none; we simply append a space to
// every word and rely on the next span continuing on the same pen — close
// enough for a notes preview and keeps spacing visible between words.
std::vector<std::string> SplitWords(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        // Skip leading spaces but emit them as part of separation: collapse
        // runs of spaces into a single trailing space on the previous word.
        size_t start = i;
        while (i < n && s[i] != ' ') ++i;
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
        // Consume the space run.
        bool hadSpace = false;
        while (i < n && s[i] == ' ') { ++i; hadSpace = true; }
        if (hadSpace && !out.empty()) {
            out.back().push_back(' ');
        } else if (hadSpace && out.empty()) {
            // Leading whitespace-only: represent as a lone space word so the
            // pen advances (rare, but avoids dropping intentional indent).
            out.push_back(std::string(" "));
        }
    }
    return out;
}

// Draw a word with the given font/size/colour at pos. style: 0=plain,
// 1=bold, 2=italic. Returns nothing; caller advances the pen by measured width.
void DrawWord(ImDrawList* dl, ImFont* font, float fontSize,
              const ImVec2& pos, ImU32 col, const char* text, int style)
{
    if (style == 1) {
        // Bold: double-draw, second copy nudged 1px right to thicken strokes.
        dl->AddText(font, fontSize, pos, col, text);
        dl->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), col, text);
        return;
    }
    if (style == 2) {
        // Italic: record vertex range, draw, then shear the new vertices.
        const int vtxStart = dl->VtxBuffer.Size;
        dl->AddText(font, fontSize, pos, col, text);
        const int vtxEnd = dl->VtxBuffer.Size;
        const float baseY = pos.y + fontSize;  // glyph baseline-ish (bottom)
        for (int v = vtxStart; v < vtxEnd; ++v) {
            ImDrawVert& vert = dl->VtxBuffer[v];
            vert.pos.x += (baseY - vert.pos.y) * kItalicShear;
        }
        return;
    }
    // Plain text.
    dl->AddText(font, fontSize, pos, col, text);
}

// Draw a chip placeholder token at `pos`. Returns its total advance width.
// Factored out so Task 13 can swap in a resolved icon+name token using the
// same pen-position contract (takes pen pos, returns consumed width).
float DrawChip(ImDrawList* dl, ImFont* font, float fontSize,
               const ImVec2& pos, const std::string& /*code*/)
{
    // Placeholder label — Task 13 replaces with the resolved name.
    const char* label = "[link]";
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);

    const float padX = 4.0f;
    const float padY = 1.0f;
    const ImVec2 rMin(pos.x, pos.y - padY);
    const ImVec2 rMax(pos.x + textSize.x + padX * 2.0f, pos.y + fontSize + padY);

    // Faint blue/gold tint behind a bracketed label.
    const ImU32 bgCol     = IM_COL32(70, 90, 150, 90);
    const ImU32 labelCol  = IM_COL32(210, 220, 255, 255);
    dl->AddRectFilled(rMin, rMax, bgCol, 3.0f);
    dl->AddText(font, fontSize, ImVec2(pos.x + padX, pos.y), labelCol, label);

    return textSize.x + padX * 2.0f;
}

} // namespace

void RenderMarkdown(const std::vector<Md::Line>& lines, ImFont* fontBig, float wrapWidth)
{
    if (wrapWidth <= 0.0f) wrapWidth = 1.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

    const float rightEdge = origin.x + wrapWidth;

    float penX = origin.x;
    float penY = origin.y;
    float maxPenY = origin.y;
    float lastLineHeight = ImGui::GetFont()->FontSize + kLeading;

    for (const Md::Line& line : lines) {
        // Choose active font.
        ImFont* font = ImGui::GetFont();
        if (line.block == Md::Block::Heading && fontBig != nullptr) {
            font = fontBig;
        }
        const float fontSize   = font->FontSize;
        const float lineHeight = fontSize + kLeading;
        lastLineHeight = lineHeight;

        // Compute this line's left edge (hanging indent for bullets).
        float startX = origin.x;
        penX = startX;

        if (line.block == Md::Block::Bullet) {
            const char* marker = "- ";
            const ImVec2 mSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, marker);
            dl->AddText(font, fontSize, ImVec2(penX, penY), textCol, marker);
            penX += mSize.x;
            startX += mSize.x;  // continuation lines hang under the text
        }

        // Lay out spans.
        for (const Md::Span& span : line.spans) {
            if (span.kind == Md::Inline::Chip) {
                // Measure chip width first so we can wrap before it if needed.
                const char* label = "[link]";
                const float chipW =
                    font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label).x + 8.0f;
                if (penX + chipW > rightEdge && penX > startX) {
                    penX = startX;
                    penY += lineHeight;
                }
                const float w = DrawChip(dl, font, fontSize, ImVec2(penX, penY), span.text);
                penX += w;
                continue;
            }

            int style = 0;
            if (span.kind == Md::Inline::Bold)   style = 1;
            else if (span.kind == Md::Inline::Italic) style = 2;

            const std::vector<std::string> words = SplitWords(span.text);
            for (const std::string& word : words) {
                const float wordW =
                    font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, word.c_str()).x;

                // Wrap before drawing, but only if there is already content on
                // this visual line (penX past start). A single word wider than
                // wrapWidth is drawn anyway (no infinite loop / no hang).
                if (penX + wordW > rightEdge && penX > startX) {
                    penX = startX;
                    penY += lineHeight;
                }

                DrawWord(dl, font, fontSize, ImVec2(penX, penY), textCol,
                         word.c_str(), style);
                penX += wordW;
            }
        }

        // End of source line: advance to the next line.
        if (penY > maxPenY) maxPenY = penY;
        penY += lineHeight;
    }

    if (penY > maxPenY) maxPenY = penY;

    // Reserve vertical space so ImGui scrolling/layout accounts for our draws.
    const float h = (maxPenY + lastLineHeight) - origin.y;
    ImGui::Dummy(ImVec2(wrapWidth, h));
}

} // namespace Magpie
