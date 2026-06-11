#pragma once
#include <string>
#include <vector>

namespace Magpie { namespace Md {

enum class Block  { Paragraph, Heading, Bullet };
enum class Inline { Text, Bold, Italic, Chip };

struct Span {
    Inline      kind;
    std::string text;   // literal display text (markers stripped) for Text/Bold/Italic;
                        // the raw "[&...]" chat code for Chip
};

struct Line {
    Block             block        = Block::Paragraph;
    int               headingLevel = 0;   // # count for Heading (1..N); 0 otherwise
    std::vector<Span> spans;
};

// Split body on '\n', one Line per source line.
// Empty body -> empty vector.
std::vector<Line> Parse(const std::string& body);

}} // namespace Magpie::Md
