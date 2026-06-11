#pragma once
// MarkdownRender.h — ImGui draw-list rendering of parsed markdown (VIEW mode).
//
// Renders the subset of markdown produced by Magpie::Md::Parse: headings,
// bullets, paragraphs, with inline bold (faux via double-draw) and italic
// (faux via per-glyph vertex shear), since Nexus exposes no bold/italic font.
// Chips render as a placeholder token for now (Task 13 replaces with real
// icon + resolved name).

#include <vector>
#include "Markdown.h"

struct ImFont;  // forward (defined in imgui.h)

namespace Magpie {

// Render parsed markdown lines at the current ImGui cursor, wrapping to
// `wrapWidth` pixels. `fontBig` is Nexus's FontBig for headings (may be null
// -> falls back to the current font). Advances ImGui layout (via Dummy) to
// cover the drawn height so the content participates in scrolling.
void RenderMarkdown(const std::vector<Md::Line>& lines, ImFont* fontBig, float wrapWidth);

} // namespace Magpie
