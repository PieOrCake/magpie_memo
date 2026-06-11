#pragma once
//
// ChipCodec -- PURE text <-> cell-model round-trip for the chip text editor.
//
// PURITY: depends ONLY on the C++ standard library and the vendored ChatLinks
// codec. NO ImGui, Nexus, Win32, or game hooks. This is the host-testable slice
// of the ChipTextEdit widget: it owns the parse (chat line -> ordered cells) and
// serialize (cells -> chat line) so the text<->chip mapping can be verified
// without a renderer.
//
// A ParsedCell is one atomic unit of the editor model: either a chip (one whole
// "[&base64]" chat code) or a single text codepoint (which may be '\n'). The
// widget's ChipCell mirrors this model, adding only per-frame render fields.
//
#include <string>
#include <vector>
#include <cstdint>
#include "chat/ChatLinks.h"

namespace Magpie {

// One atomic cell of the editor model.
struct ParsedCell {
    bool        isChip = false;  // true: a whole chat code; false: a single text codepoint
    uint32_t    cp = 0;          // Unicode codepoint (text cell; may be '\n')
    std::string code;            // "[&base64]" canonical chat code (chip cell)
};

// Parse a UTF-8 string into ordered cells: each chat link / AE2 code becomes ONE
// chip cell ({isChip=true, code}); all other text (incl. '\n') becomes one text
// cell per codepoint ({isChip=false, cp}).
std::vector<ParsedCell> ParseToCells(const std::string& utf8);

// Reassemble cells back into a UTF-8 string: a chip emits its code verbatim; a
// text cell emits its codepoint's UTF-8 (newlines preserved). Inverse of
// ParseToCells for any string it produced.
std::string CellsToText(const std::vector<ParsedCell>& cells);

// --- UTF-8 helpers (shared with the widget) ---
void                      AppendUtf8(std::string& s, uint32_t cp);
std::vector<uint32_t>     DecodeUtf8(const std::string& s);

} // namespace Magpie
