#include "Markdown.h"
#include "chat/ChatLinks.h"
#include <algorithm>

namespace Magpie { namespace Md {

// ---------------------------------------------------------------------------
// Inline emphasis tokeniser — processes a plain-text segment for **bold** and
// *italic*, emitting Span entries into `out`. Unmatched markers are literal.
// Bold (double-star) is processed before italic (single-star) to avoid
// misreading "**x**" as two partial italics.
// ---------------------------------------------------------------------------
static void TokeniseEmphasis(const std::string& s, std::vector<Span>& out)
{
    // We scan character by character.  At each '*', we check whether there's a
    // matching partner further right and, if so, emit the emphasised run.
    // Otherwise the '*' is emitted as literal text.

    std::size_t i = 0;
    std::size_t n = s.size();
    std::string buf;   // accumulates plain-text characters

    auto flushBuf = [&]() {
        if (!buf.empty()) {
            out.push_back({ Inline::Text, std::move(buf) });
            buf.clear();
        }
    };

    while (i < n) {
        if (i + 1 < n && s[i] == '*' && s[i + 1] == '*') {
            // Try to find a matching closing "**"
            std::size_t close = s.find("**", i + 2);
            if (close != std::string::npos) {
                flushBuf();
                out.push_back({ Inline::Bold, s.substr(i + 2, close - (i + 2)) });
                i = close + 2;
            } else {
                // Unmatched — emit both stars as literal
                buf += '*';
                buf += '*';
                i += 2;
            }
        } else if (s[i] == '*') {
            // Try to find a matching closing single "*" (not "**")
            std::size_t j = i + 1;
            bool found = false;
            while (j < n) {
                if (s[j] == '*') {
                    // Make sure it's not a "**"
                    if (j + 1 < n && s[j + 1] == '*') {
                        // skip — this is a double-star, not a single closer
                        j += 2;
                    } else {
                        found = true;
                        break;
                    }
                } else {
                    ++j;
                }
            }
            if (found) {
                flushBuf();
                out.push_back({ Inline::Italic, s.substr(i + 1, j - (i + 1)) });
                i = j + 1;
            } else {
                // Unmatched
                buf += '*';
                ++i;
            }
        } else {
            buf += s[i];
            ++i;
        }
    }
    flushBuf();
}

// ---------------------------------------------------------------------------
// Parse the inline content of one source line (block markers already stripped).
// Delegates to ChatLinks::SegmentLine for chip detection, then tokenises each
// Plain segment for emphasis.
// ---------------------------------------------------------------------------
static std::vector<Span> ParseInline(const std::string& content)
{
    using namespace PieUI::ChatLinks;
    std::vector<Span> result;

    auto segments = SegmentLine(content);
    for (const auto& seg : segments) {
        if (seg.kind == SegmentKind::Link) {
            // Chat link chip — raw "[&...]" code
            result.push_back({ Inline::Chip, seg.raw });
        } else if (seg.kind == SegmentKind::Plain) {
            // Tokenise for emphasis
            TokeniseEmphasis(seg.raw, result);
        } else {
            // URL or AE2 — treat as plain text using the raw source slice
            result.push_back({ Inline::Text, seg.raw });
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Parse a single source line into a Line (block type + inline spans).
// ---------------------------------------------------------------------------
static Line ParseLine(const std::string& src)
{
    Line line;

    // --- Heading detection: leading '#'s followed by a space ---
    if (!src.empty() && src[0] == '#') {
        int level = 0;
        std::size_t i = 0;
        while (i < src.size() && src[i] == '#') { ++level; ++i; }
        if (i < src.size() && src[i] == ' ') {
            line.block        = Block::Heading;
            line.headingLevel = level;
            line.spans        = ParseInline(src.substr(i + 1));
            return line;
        }
        // '#' not followed by space — fall through to paragraph
    }

    // --- Bullet detection: optional leading spaces, then "- " ---
    {
        std::size_t i = 0;
        while (i < src.size() && src[i] == ' ') ++i;
        if (i + 1 < src.size() && src[i] == '-' && src[i + 1] == ' ') {
            line.block = Block::Bullet;
            line.spans = ParseInline(src.substr(i + 2));
            return line;
        }
    }

    // --- Paragraph ---
    line.block = Block::Paragraph;
    line.spans = ParseInline(src);
    return line;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
std::vector<Line> Parse(const std::string& body)
{
    if (body.empty()) return {};

    std::vector<Line> result;
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t end = body.find('\n', start);
        if (end == std::string::npos) {
            result.push_back(ParseLine(body.substr(start)));
            break;
        }
        result.push_back(ParseLine(body.substr(start, end - start)));
        start = end + 1;
    }
    // A trailing newline produces a final empty Paragraph line — that's
    // consistent (the caller can skip empty lines during rendering).
    return result;
}

}} // namespace Magpie::Md
