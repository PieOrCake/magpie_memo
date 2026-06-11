#pragma once
#include "imgui.h"
#include <string>
#include <vector>

// A MULTI-LINE text editor that embeds chat-link "chips" as ATOMIC, inline elements -- matching the
// vanilla GW2 chatbox: the caret moves over a chip as one unit, one backspace/delete adjacent to it
// removes the whole chip, and all surrounding text stays fully editable. Built on stb_textedit (the
// same engine ImGui's own InputText uses), with per-element widths so a chip renders as a wide
// coloured token while caret/mouse/selection all "just work".
//
// Adapted from Pie UI's single-line ChipTextEdit and converted to MULTI-LINE for Magpie Memo's note
// editor: a newline is a non-chip cell (cp == '\n'), the layout maps (x,y) clicks to rows, Up/Down
// move between rows, and Enter INSERTS a newline (it does NOT submit). Decoupled from Pie UI's
// RichLine/game hooks: chip labels + colours come solely from Magpie::ResolveChip + the vendored
// ChatLinks codec. NO texture cache, NO game memory reads.
//
// Each surface owns one instance. It manages its own keyboard focus (claimed on click) and only
// captures keys while focused, so it coexists with the rest of the UI.
namespace Magpie {

// "The chip input was focused very recently" (short timeout). The custom widget's WantTextInput
// flickers false at the start of each frame (no public next-frame setter), so the WndProc
// occasionally forwards a key to the game (e.g. 'h' opens Hero, WASD moves). The focused Render
// stamps a timestamp; the WndProc reads this to reinforce the capture flags for keyboard messages
// (no consume). A timeout (not a per-frame reset) keeps it CONTINUOUSLY true while typing -- no
// reset-gap for a held key to slip through -- and self-clears shortly after focus is lost.
bool ChipInputActive();

// Release focus from every chip input on the next frame each renders. Called by the WndProc when a
// click lands in the game world (no overlay window under it) -- such a click never reaches ImGui's
// mouse state, so the in-widget "clicked elsewhere" release can't see it.
void RequestChipBlur();

struct ChipCell {
    bool         isChip = false;
    unsigned int cp = 0;      // Unicode codepoint (text cell); '\n' marks a newline cell
    std::string  code;        // "[&base64]" (chip cell) -- the canonical send form
    // Per-frame render cache (filled at the top of Render):
    float        width = 0.0f;
    std::string  name;        // resolved bracketed label, e.g. "[Copper Ore]"
    ImU32        color = 0;
};

struct ChipTextEdit {
    std::vector<ChipCell> cells;
    void*  m_stb      = nullptr;   // opaque ::STB_TexteditState (kept out of the header to avoid
                                   // tripping ImGui's own imstb_textedit.h include guard)
    bool   focused    = false;
    bool   focusNext  = false;
    float  scrollX    = 0.0f;      // (unused in multi-line; kept for ABI/state symmetry)
    unsigned int blurSeen = 0;     // last RequestChipBlur() sequence this instance acted on

    ~ChipTextEdit();

    // Render a multi-line editor at the current ImGui cursor for `width` px, sized to its row count
    // plus a minimum of `minLines` rows. Returns true if the content changed this frame.
    bool        Render(const char* id, float width, ImU32 borderCol, int minLines = 3);
    void        InsertChip(const std::string& code);   // at the caret, as one atomic cell
    void        SetText(const std::string& utf8);      // replace all (parses [&..] back into chips)
    std::string GetText() const;                       // codes + text, in order (incl. newlines)
    bool        Empty() const { return cells.empty(); }
    void        Clear();
    void        Focus()       { focusNext = true; }
    bool        IsFocused() const { return focused; }
};

} // namespace Magpie
