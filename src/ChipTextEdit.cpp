#include "ChipTextEdit.h"
#include "ChipResolver.h"      // Magpie::ResolveChip (replaces Pie UI's RichLineResolveChip)
#include "ChipCodec.h"         // pure text<->cell model + UTF-8 helpers
#include "chat/ChatLinks.h"    // SegmentLine (paste: parse [&..] into chips)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

// stb_textedit, included in two passes (the standard pattern ImGui itself uses): first for the
// TYPES (StbTexteditRow / STB_TexteditState -- default CHARTYPE int), then again with our macros +
// IMPLEMENTATION (the include guard skips the types the 2nd time). We deliberately do NOT include
// imgui_internal.h, so this is the ONLY stb_textedit instance in the TU -- no guard/symbol clash
// with ImGui's own ImStb-namespaced copy.
#include "imstb_textedit.h"    // pass 1: types only

static inline int   ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- stb_textedit glue ------------------------------------------------------------------------
// Our STRING type is ChipTextEdit; each "character" is a ChipCell (text codepoint OR an atomic
// chip). Widths are per-cell so a chip occupies its rendered token width. A newline cell
// (isChip==false, cp=='\n') has the sentinel width so stb treats it as a line break.
namespace Magpie {

static inline bool IsNewlineCell(const ChipCell& c) { return !c.isChip && c.cp == '\n'; }

static int   ChipTE_StringLen(ChipTextEdit* o)            { return (int)o->cells.size(); }
static int   ChipTE_GetChar(ChipTextEdit* o, int i)       { return o->cells[i].isChip ? 0xFFFC : (int)o->cells[i].cp; }
static float ChipTE_GetWidth(ChipTextEdit* o, int /*ls*/, int i) {
    // -1.0f == STB_TEXTEDIT_GETWIDTH_NEWLINE (defined below); return the literal so we don't depend
    // on macro visibility order. stb compares the returned width against the sentinel.
    return IsNewlineCell(o->cells[i]) ? -1.0f : o->cells[i].width;
}

// Lay out ONE row beginning at cell `n`: sum the widths of this row's NON-newline cells, and count
// cells until (and including) a trailing newline cell, or to end of text. This is what lets stb map
// (x,y) clicks to the right row and Up/Down move between rows.
static void  ChipTE_Layout(StbTexteditRow* r, ChipTextEdit* o, int n) {
    const float lh = ImGui::GetTextLineHeight();
    const int total = (int)o->cells.size();
    float w = 0.0f;
    int count = 0;
    for (int i = n; i < total; ++i) {
        ++count;
        if (IsNewlineCell(o->cells[i])) break;  // include the newline cell, then stop
        w += o->cells[i].width;
    }
    r->x0 = 0.0f;
    r->x1 = w;
    r->baseline_y_delta = lh;
    r->ymin = 0.0f;
    r->ymax = lh;
    r->num_chars = count;
}

static void  ChipTE_DeleteChars(ChipTextEdit* o, int i, int n) {
    if (i < 0 || n <= 0 || i >= (int)o->cells.size()) return;
    n = std::min(n, (int)o->cells.size() - i);
    o->cells.erase(o->cells.begin() + i, o->cells.begin() + i + n);
}
static int   ChipTE_InsertChars(ChipTextEdit* o, int i, const int* chars, int n) {
    if (i < 0 || i > (int)o->cells.size()) return 0;
    std::vector<ChipCell> ins; ins.reserve(n);
    // Arbitrary codepoints, including '\n' (a newline typed via stb_textedit_key('\n')), become
    // ordinary text cells -- IsNewlineCell() then recognises them.
    for (int k = 0; k < n; ++k) { ChipCell c; c.isChip = false; c.cp = (unsigned)chars[k]; ins.push_back(std::move(c)); }
    o->cells.insert(o->cells.begin() + i, ins.begin(), ins.end());
    return 1;
}

} // namespace Magpie

// stb_textedit configuration (mirrors ImGui's, so editing behaviour matches its InputText).
#define STB_TEXTEDIT_CHARTYPE       int
#define STB_TEXTEDIT_POSITIONTYPE   int
#define STB_TEXTEDIT_STRING         Magpie::ChipTextEdit
#define STB_TEXTEDIT_STRINGLEN(o)            Magpie::ChipTE_StringLen(o)
#define STB_TEXTEDIT_GETCHAR(o,i)            Magpie::ChipTE_GetChar(o,i)
#define STB_TEXTEDIT_GETWIDTH(o,ls,i)        Magpie::ChipTE_GetWidth(o,ls,i)
#define STB_TEXTEDIT_LAYOUTROW(r,o,ls)       Magpie::ChipTE_Layout(r,o,ls)
#define STB_TEXTEDIT_DELETECHARS(o,i,n)      Magpie::ChipTE_DeleteChars(o,i,n)
#define STB_TEXTEDIT_INSERTCHARS(o,i,c,n)    Magpie::ChipTE_InsertChars(o,i,c,n)
#define STB_TEXTEDIT_KEYTOTEXT(k)            ((k) >= 0x200000 ? 0 : (k))
#define STB_TEXTEDIT_NEWLINE                 '\n'
#define STB_TEXTEDIT_GETWIDTH_NEWLINE        (-1.0f)
#define STB_TEXTEDIT_K_LEFT       0x200000
#define STB_TEXTEDIT_K_RIGHT      0x200001
#define STB_TEXTEDIT_K_UP         0x200002
#define STB_TEXTEDIT_K_DOWN       0x200003
#define STB_TEXTEDIT_K_LINESTART  0x200004
#define STB_TEXTEDIT_K_LINEEND    0x200005
#define STB_TEXTEDIT_K_TEXTSTART  0x200006
#define STB_TEXTEDIT_K_TEXTEND    0x200007
#define STB_TEXTEDIT_K_DELETE     0x200008
#define STB_TEXTEDIT_K_BACKSPACE  0x200009
#define STB_TEXTEDIT_K_UNDO       0x20000A
#define STB_TEXTEDIT_K_REDO       0x20000B
#define STB_TEXTEDIT_K_WORDLEFT   0x20000C
#define STB_TEXTEDIT_K_WORDRIGHT  0x20000D
#define STB_TEXTEDIT_K_PGUP       0x20000E
#define STB_TEXTEDIT_K_PGDOWN     0x20000F
#define STB_TEXTEDIT_K_SHIFT      0x400000
#define STB_TEXTEDIT_IMPLEMENTATION
#include "imstb_textedit.h"

namespace Magpie {

// Focus timestamp (ms, steady clock) for the WndProc keyboard-capture reinforcement (see header).
static std::atomic<long long> s_lastFocusMs{-100000};
static long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static void StampChipFocus() { s_lastFocusMs.store(NowMs(), std::memory_order_relaxed); }
bool ChipInputActive() {
    constexpr long long kHold = 60;   // ms; > a slow frame, < a perceptible input lag
    return (NowMs() - s_lastFocusMs.load(std::memory_order_relaxed)) < kHold;
}

// Monotonic blur request: the WndProc bumps it on a game-world click; each chip input releases
// focus once per bump (compared against its own blurSeen) on its next Render.
static std::atomic<unsigned int> s_blurSeq{0};
void RequestChipBlur() { s_blurSeq.fetch_add(1, std::memory_order_relaxed); }

// Lazily allocate + initialise the (opaque) stb state. is_single_line = 0 -> MULTI-LINE.
static STB_TexteditState& St(ChipTextEdit* o) {
    if (!o->m_stb) { auto* s = new STB_TexteditState(); stb_textedit_initialize_state(s, 0); o->m_stb = s; }
    return *static_cast<STB_TexteditState*>(o->m_stb);
}

ChipTextEdit::~ChipTextEdit() { delete static_cast<STB_TexteditState*>(m_stb); m_stb = nullptr; }

// ---- per-frame cell measurement ---------------------------------------------------------------
static void Measure(std::vector<ChipCell>& cells) {
    ImFont* font = ImGui::GetFont();
    const float scale = ImGui::GetFontSize() / font->FontSize;
    for (auto& c : cells) {
        if (c.isChip) {
            ChipView v = Magpie::ResolveChip(c.code);
            std::string name = v.label.empty() ? (c.name.empty() ? "[link]" : c.name) : v.label;
            c.name = name; c.color = v.color;
            c.width = ImGui::CalcTextSize(name.c_str()).x + 6.0f;   // small side padding
        } else if (IsNewlineCell(c)) {
            c.width = 0.0f;   // visual width; the GETWIDTH sentinel is handled in ChipTE_GetWidth
        } else {
            c.width = font->GetCharAdvance((ImWchar)c.cp) * scale;
        }
    }
}

void ChipTextEdit::Clear() { cells.clear(); stb_textedit_initialize_state(&St(this), 0); }

void ChipTextEdit::InsertChip(const std::string& code) {
    STB_TexteditState& s = St(this);
    int at = ClampI(s.cursor, 0, (int)cells.size());
    ChipCell c; c.isChip = true; c.code = code;
    ChipView v = Magpie::ResolveChip(code);
    c.name = v.label.empty() ? "[link]" : v.label;
    c.color = v.color;
    cells.insert(cells.begin() + at, std::move(c));
    s.cursor = at + 1;
    s.select_start = s.select_end = s.cursor;
}

std::string ChipTextEdit::GetText() const {
    std::string out;
    for (const auto& c : cells) { if (c.isChip) out += c.code; else AppendUtf8(out, c.cp); }
    return out;
}

void ChipTextEdit::SetText(const std::string& utf8) {
    cells.clear();
    // Delegate the text->cell parse to the pure ChipCodec, then attach the widget's render fields.
    for (const auto& pc : ParseToCells(utf8)) {
        ChipCell c; c.isChip = pc.isChip; c.cp = pc.cp; c.code = pc.code;
        if (c.isChip) {
            ChipView v = Magpie::ResolveChip(c.code);
            c.name = v.label.empty() ? "[link]" : v.label;
            c.color = v.color;
        }
        cells.push_back(std::move(c));
    }
    STB_TexteditState& s = St(this);
    s.cursor = (int)cells.size();
    s.select_start = s.select_end = s.cursor;
}

// ---- row / position geometry ------------------------------------------------------------------
// Number of rows = newline cells + 1.
static int RowCount(const std::vector<ChipCell>& cells) {
    int rows = 1;
    for (const auto& c : cells) if (IsNewlineCell(c)) ++rows;
    return rows;
}

// Map caret index `i` (0..size) to (row, x-offset-within-row). x is the sum of NON-newline cell
// widths since the start of the row containing position i.
static void CaretPos(const std::vector<ChipCell>& cells, int i, int& row, float& x) {
    row = 0; x = 0.0f;
    const int n = (int)cells.size();
    if (i > n) i = n;
    for (int k = 0; k < i; ++k) {
        if (IsNewlineCell(cells[k])) { ++row; x = 0.0f; }
        else                          { x += cells[k].width; }
    }
}

bool ChipTextEdit::Render(const char* id, float width, ImU32 borderCol, int minLines) {
    STB_TexteditState& s = St(this);
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    Measure(cells);
    const float lh = ImGui::GetTextLineHeight();

    const int rows = std::max(RowCount(cells), std::max(1, minLines));
    const float contentH = rows * lh;
    const float h = contentH + style.FramePadding.y * 2.0f;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    // Focus: claim on click inside; release on a click that isn't ours.
    if (ImGui::IsItemClicked()) focused = true;
    else if (io.MouseClicked[0] && !hovered) focused = false;
    {
        unsigned int seq = s_blurSeq.load(std::memory_order_relaxed);
        if (seq != blurSeen) { blurSeen = seq; if (!ImGui::IsItemClicked()) focused = false; }
    }
    if (focusNext) { focused = true; focusNext = false; }
    if (focused) {
        ImGui::CaptureKeyboardFromApp(true);
        io.WantCaptureKeyboard = true;
        io.WantTextInput       = true;
        StampChipFocus();
    }

    const ImVec2 inner_min(p0.x + style.FramePadding.x, p0.y + style.FramePadding.y);
    const float  inner_w = width - style.FramePadding.x * 2.0f;

    bool changed = false;
    const int sizeBefore = (int)cells.size();

    // ---- input (only while focused) ----
    if (focused) {
        // Mouse: map screen (x,y) to text-local coords, real y so clicks land on the right row.
        const float mx = io.MousePos.x - inner_min.x;
        const float my = io.MousePos.y - inner_min.y;
        if (ImGui::IsItemClicked()) stb_textedit_click(this, &s, mx, my);
        else if (held && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) stb_textedit_drag(this, &s, mx, my);

        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c != 0x7F) { stb_textedit_key(this, &s, c); changed = true; }
        }
        auto K = [&](ImGuiKey k){ int idx = io.KeyMap[k]; return idx >= 0 && ImGui::IsKeyPressed(idx); };
        const int sh = io.KeyShift ? STB_TEXTEDIT_K_SHIFT : 0;
        const bool word = io.KeyCtrl;
        if (K(ImGuiKey_LeftArrow))   stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDLEFT  : STB_TEXTEDIT_K_LEFT)  | sh);
        if (K(ImGuiKey_RightArrow))  stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | sh);
        if (K(ImGuiKey_UpArrow))     stb_textedit_key(this, &s, STB_TEXTEDIT_K_UP   | sh);
        if (K(ImGuiKey_DownArrow))   stb_textedit_key(this, &s, STB_TEXTEDIT_K_DOWN | sh);
        if (K(ImGuiKey_PageUp))      stb_textedit_key(this, &s, STB_TEXTEDIT_K_PGUP | sh);
        if (K(ImGuiKey_PageDown))    stb_textedit_key(this, &s, STB_TEXTEDIT_K_PGDOWN | sh);
        if (K(ImGuiKey_Home))        stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINESTART | sh);
        if (K(ImGuiKey_End))         stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINEEND | sh);
        if (K(ImGuiKey_Delete))    { stb_textedit_key(this, &s, STB_TEXTEDIT_K_DELETE | sh); changed = true; }
        if (K(ImGuiKey_Backspace)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_BACKSPACE | sh); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_Z)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_UNDO); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_Y)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_REDO); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_A)) { s.select_start = 0; s.select_end = (int)cells.size(); s.cursor = (int)cells.size(); }
        if (io.KeyCtrl && (K(ImGuiKey_C) || K(ImGuiKey_X))) {
            int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
            if (b > a) {
                std::string sub; for (int k = a; k < b; ++k) { if (cells[k].isChip) sub += cells[k].code; else AppendUtf8(sub, cells[k].cp); }
                ImGui::SetClipboardText(sub.c_str());
                if (K(ImGuiKey_X)) { stb_textedit_cut(this, &s); changed = true; }
            }
        }
        if (io.KeyCtrl && K(ImGuiKey_V)) {
            const char* clip = ImGui::GetClipboardText();
            if (clip && *clip) {
                for (const auto& seg : PieUI::ChatLinks::SegmentLine(clip)) {
                    if (seg.kind == PieUI::ChatLinks::SegmentKind::Link ||
                        seg.kind == PieUI::ChatLinks::SegmentKind::Ae2) {
                        InsertChip(seg.raw);
                    } else {
                        const std::string& t = seg.raw.empty() ? seg.display : seg.raw;
                        for (unsigned int cp : DecodeUtf8(t)) if (cp >= 0x20 || cp == '\n') stb_textedit_key(this, &s, (int)cp);
                    }
                }
                changed = true;
            }
        }
        // MULTI-LINE: Enter INSERTS a newline (it does NOT submit).
        if (K(ImGuiKey_Enter) || K(ImGuiKey_KeyPadEnter)) { stb_textedit_key(this, &s, '\n'); changed = true; }
        if (K(ImGuiKey_Escape)) focused = false;

        Measure(cells);   // input may have changed cells; re-measure for the draw below
    }
    if ((int)cells.size() != sizeBefore) changed = true;

    // ---- draw ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + h);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (borderCol & 0xFF000000) dl->AddRect(p0, p1, borderCol, style.FrameRounding);

    dl->PushClipRect(ImVec2(inner_min.x, p0.y), ImVec2(p1.x - style.FramePadding.x, p1.y), true);

    // Selection: one rect per row the [a,b) range covers.
    if (focused && s.select_start != s.select_end) {
        int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
        int rowA; float xA; CaretPos(cells, a, rowA, xA);
        // Walk from a to b, emitting a rect when we cross a row boundary or reach b.
        int curRow = rowA; float rowStartX = xA; float x = xA;
        const ImU32 selCol = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
        auto emitSel = [&](int rr, float x0, float x1) {
            if (x1 <= x0) x1 = x0 + 2.0f;   // a selected newline shows a thin marker
            float yy = inner_min.y + rr * lh;
            dl->AddRectFilled(ImVec2(inner_min.x + x0, yy), ImVec2(inner_min.x + x1, yy + lh), selCol);
        };
        for (int k = a; k < b; ++k) {
            if (IsNewlineCell(cells[k])) {
                emitSel(curRow, rowStartX, x);
                ++curRow; rowStartX = 0.0f; x = 0.0f;
            } else {
                x += cells[k].width;
            }
        }
        emitSel(curRow, rowStartX, x);
    }

    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    float x = inner_min.x;
    float y = inner_min.y;
    std::string run; float runStart = x;
    auto flushRun = [&]() {
        if (!run.empty()) { dl->AddText(ImVec2(runStart, y), textCol, run.c_str()); run.clear(); }
    };
    for (auto& c : cells) {
        if (c.isChip) {
            flushRun();
            ImU32 bg = (c.color & 0x00FFFFFF) | 0x33000000;   // faint tint of the chip colour
            dl->AddRectFilled(ImVec2(x + 1, y), ImVec2(x + c.width - 1, y + lh), bg, 3.0f);
            dl->AddText(ImVec2(x + 3, y), c.color, c.name.c_str());
            x += c.width; runStart = x;
        } else if (IsNewlineCell(c)) {
            flushRun();
            x = inner_min.x; runStart = x;
            y += lh;
        } else {
            AppendUtf8(run, c.cp);
            x += c.width;
        }
    }
    flushRun();

    // Caret at the cursor's (row, x).
    if (focused) {
        int crow; float cx; CaretPos(cells, s.cursor, crow, cx);
        float caretX = inner_min.x + cx;
        float caretY = inner_min.y + crow * lh;
        if (std::fmod((float)ImGui::GetTime(), 1.0f) < 0.6f)
            dl->AddLine(ImVec2(caretX, caretY), ImVec2(caretX, caretY + lh), textCol);
    }

    dl->PopClipRect();
    return changed;
}

} // namespace Magpie
