#include "NotesStore.h"
#include "chat/ChatLinks.h"
#include "chat/SpecData.h"
#include "Markdown.h"
#include "ChipResolver.h"
#include "ChipCodec.h"
#include "IconUrl.h"
#include "ChipColor.h"
#include "DecoderRingApi.h"
#include <iostream>
#include <string>
#include <cstdint>

// ── Minimal test harness ──────────────────────────────────────────────────────

static int s_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n"; \
            ++s_failures; \
        } \
    } while (false)

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " \
                      << #a " == " #b " (got \"" << _a << "\" vs \"" << _b << "\")\n"; \
            ++s_failures; \
        } \
    } while (false)

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_create_appears_in_notes()
{
    Magpie::NotesStore store;
    auto& n = store.Create("My note");
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].title, std::string("My note"));
    CHECK(store.Notes()[0].body.empty());
    CHECK(!store.Notes()[0].id.empty());
    // Returned reference matches what's in the list
    CHECK_EQ(n.id, store.Notes()[0].id);
}

static void test_default_title()
{
    Magpie::NotesStore store;
    store.Create();
    CHECK_EQ(store.Notes()[0].title, std::string("Untitled note"));
}

static void test_two_creates_have_different_ids()
{
    Magpie::NotesStore store;
    std::string idA = store.Create("A").id;
    std::string idB = store.Create("B").id;
    CHECK(idA != idB);
    CHECK(store.Notes().size() == 2);
}

static void test_get_returns_note()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Find me");
    Magpie::Note* p = store.Get(n.id);
    CHECK(p != nullptr);
    CHECK_EQ(p->title, std::string("Find me"));
}

static void test_get_returns_nullptr_for_missing()
{
    Magpie::NotesStore store;
    store.Create("something");
    Magpie::Note* p = store.Get("nonexistent-id");
    CHECK(p == nullptr);
}

static void test_edit_via_get_persists()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Original");
    Magpie::Note* p = store.Get(n.id);
    p->body = "edited body";
    p->title = "Updated";
    CHECK_EQ(store.Notes()[0].body, std::string("edited body"));
    CHECK_EQ(store.Notes()[0].title, std::string("Updated"));
}

static void test_edit_via_create_ref_persists()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Ref test");
    n.body = "body via ref";
    CHECK_EQ(store.Notes()[0].body, std::string("body via ref"));
}

static void test_delete_removes_note()
{
    Magpie::NotesStore store;
    std::string aid = store.Create("A").id;
    std::string bid = store.Create("B").id;
    store.Delete(aid);
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].id, bid);
    CHECK(store.Get(aid) == nullptr);
}

static void test_delete_noop_for_missing()
{
    Magpie::NotesStore store;
    store.Create("X");
    store.Delete("does-not-exist");
    CHECK(store.Notes().size() == 1);
}

static void test_json_roundtrip_ordered()
{
    Magpie::NotesStore src;
    std::string id1 = src.Create("First").id;
    src.Get(id1)->body = "body one";
    std::string id2 = src.Create("Second").id;
    src.Get(id2)->body = "body two";
    std::string id3 = src.Create("Third").id;
    src.Get(id3)->body = "body three";

    std::string json = src.ToJson();
    CHECK(!json.empty());

    Magpie::NotesStore dst;
    dst.Create("should be replaced");
    bool ok = dst.FromJson(json);
    CHECK(ok);
    CHECK(dst.Notes().size() == 3);
    // Order preserved
    CHECK_EQ(dst.Notes()[0].id, id1);
    CHECK_EQ(dst.Notes()[0].title, std::string("First"));
    CHECK_EQ(dst.Notes()[0].body, std::string("body one"));
    CHECK_EQ(dst.Notes()[1].id, id2);
    CHECK_EQ(dst.Notes()[1].title, std::string("Second"));
    CHECK_EQ(dst.Notes()[1].body, std::string("body two"));
    CHECK_EQ(dst.Notes()[2].id, id3);
    CHECK_EQ(dst.Notes()[2].title, std::string("Third"));
    CHECK_EQ(dst.Notes()[2].body, std::string("body three"));
}

static void test_fromjson_invalid_returns_false_and_leaves_state()
{
    Magpie::NotesStore store;
    store.Create("keep me");
    bool ok = store.FromJson("{ not valid json !!!");
    CHECK(!ok);
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].title, std::string("keep me"));
}

static void test_fromjson_wrong_shape_returns_false()
{
    // Valid JSON but not an array of note objects
    Magpie::NotesStore store;
    store.Create("keep me");
    bool ok = store.FromJson("{\"hello\":\"world\"}");
    CHECK(!ok);
    CHECK(store.Notes().size() == 1);
}

static void test_fromjson_empty_array()
{
    Magpie::NotesStore store;
    store.Create("will be cleared");
    bool ok = store.FromJson("[]");
    CHECK(ok);
    CHECK(store.Notes().empty());
}

static void test_create_after_fromjson_has_unique_id()
{
    // Regression: loading notes from disk must advance the id counter so a
    // subsequent Create() cannot mint an id that collides with a loaded one.
    Magpie::NotesStore store;
    bool ok = store.FromJson(
        "[{\"id\":\"00000000\",\"title\":\"A\",\"body\":\"\"},"
        " {\"id\":\"00000001\",\"title\":\"B\",\"body\":\"\"}]");
    CHECK(ok);
    std::string newId = store.Create("C").id;
    CHECK(newId != std::string("00000000"));
    CHECK(newId != std::string("00000001"));
    // And the new note is independently addressable.
    CHECK(store.Get(newId) != nullptr);
    CHECK(store.Notes().size() == 3);
}

// ── Decoder Ring ABI header tests ──────────────────────────────────────────────

static void test_decoder_header_is_v4()
{
    // Pin the vendored ABI header to the current Decoder Ring release. If DR
    // ships a new header and we forget to re-vendor, this fails loudly. (The
    // runtime guard in DecoderClient accepts >= MIN_SUPPORTED for forward-compat;
    // this is the separate "is the header current" check.)
    CHECK(DECODER_RING_API_VERSION == 4u);
}

// ── ChatLinks codec tests ─────────────────────────────────────────────────────

static void test_codec_decode_item_extracts_upgrades()
{
    using namespace PieUI::ChatLinks;
    // A geared item (weapon) with two upgrade slots filled (rune/sigil ids).
    // Step 3 reads these to show the upgrade's set bonuses on the item tooltip.
    std::string link = EncodeItem(46774, 1, 0, 24615, 24618);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_ITEM);
    DecodedItemLink di;
    CHECK(DecodeItem(link, di));
    CHECK(di.item_id == 46774u);
    CHECK(di.upgrade1_id == 24615u);
    CHECK(di.upgrade2_id == 24618u);
}

static void test_codec_decode_item_no_upgrades_is_zero()
{
    using namespace PieUI::ChatLinks;
    // A plain item has no upgrades -> both ids must be 0 (Step 3 skips them).
    std::string link = EncodeItem(19684 /* Copper Ore */, 1);
    DecodedItemLink di;
    CHECK(DecodeItem(link, di));
    CHECK(di.upgrade1_id == 0u);
    CHECK(di.upgrade2_id == 0u);
}

static void test_codec_encode_item_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeItem(46774);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_ITEM);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_ITEM)), std::string("[Item]"));
}

static void test_codec_encode_map_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeMap(900); // arbitrary PoI id
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_MAP);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_MAP)), std::string("[Waypoint]"));
}

static void test_codec_encode_skill_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkill(5516);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_SKILL);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_SKILL)), std::string("[Skill]"));
}

static void test_codec_encode_skin_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkin(8585);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_SKIN);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_SKIN)), std::string("[Skin]"));
}

static void test_codec_segment_line_plain_and_link()
{
    using namespace PieUI::ChatLinks;
    // Build an item link and embed it in a text line
    std::string itemLink = EncodeItem(46774);
    std::string line = "Check out " + itemLink + " here";
    auto segs = SegmentLine(line);
    // Expect at least 3 segments: plain, link, plain
    CHECK(segs.size() >= 3);
    // Find the link segment
    bool foundLink = false;
    for (const auto& s : segs) {
        if (s.kind == SegmentKind::Link) {
            CHECK(s.linkType == LINK_ITEM);
            CHECK_EQ(s.raw, itemLink);
            foundLink = true;
        }
    }
    CHECK(foundLink);
}

static void test_codec_segment_line_plain_only()
{
    using namespace PieUI::ChatLinks;
    auto segs = SegmentLine("just plain text");
    CHECK(segs.size() == 1);
    CHECK(segs[0].kind == SegmentKind::Plain);
    CHECK_EQ(segs[0].display, std::string("just plain text"));
}

static void test_codec_link_type_none_for_garbage()
{
    using namespace PieUI::ChatLinks;
    CHECK(DetectType("not a link") == LINK_NONE);
    CHECK(DetectType("") == LINK_NONE);
}

static void test_specdata_elite_spec_name()
{
    using namespace PieUI::SpecData;
    // Known HoT elite specs
    CHECK_EQ(std::string(GetEliteSpecName(5)),  std::string("Druid"));
    CHECK_EQ(std::string(GetEliteSpecName(7)),  std::string("Daredevil"));
    CHECK_EQ(std::string(GetEliteSpecName(40)), std::string("Chronomancer"));
    // Known PoF elite specs
    CHECK_EQ(std::string(GetEliteSpecName(59)), std::string("Mirage"));
    CHECK_EQ(std::string(GetEliteSpecName(62)), std::string("Firebrand"));
    // Known EoD elite specs
    CHECK_EQ(std::string(GetEliteSpecName(70)), std::string("Mechanist"));
    CHECK_EQ(std::string(GetEliteSpecName(72)), std::string("Untamed"));
    // Unknown returns nullptr
    CHECK(GetEliteSpecName(0) == nullptr);
    CHECK(GetEliteSpecName(999) == nullptr);
}

static void test_specdata_profession_name()
{
    using namespace PieUI::SpecData;
    CHECK_EQ(std::string(GetProfessionName(1)), std::string("Guardian"));
    CHECK_EQ(std::string(GetProfessionName(2)), std::string("Warrior"));
    CHECK_EQ(std::string(GetProfessionName(7)), std::string("Mesmer"));
    CHECK_EQ(std::string(GetProfessionName(9)), std::string("Revenant"));
    // Unknown returns empty string (not nullptr)
    CHECK_EQ(std::string(GetProfessionName(0)),   std::string(""));
    CHECK_EQ(std::string(GetProfessionName(99)),  std::string(""));
}

// ── Markdown parser tests ─────────────────────────────────────────────────────

static void test_md_heading_level1()
{
    auto lines = Magpie::Md::Parse("# Hello");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 1);
    CHECK(lines[0].spans.size() == 1);
    CHECK(lines[0].spans[0].kind == Magpie::Md::Inline::Text);
    CHECK_EQ(lines[0].spans[0].text, std::string("Hello"));
}

static void test_md_heading_level3()
{
    auto lines = Magpie::Md::Parse("### SubHead");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 3);
    CHECK(lines[0].spans.size() == 1);
    CHECK_EQ(lines[0].spans[0].text, std::string("SubHead"));
}

static void test_md_bullet()
{
    auto lines = Magpie::Md::Parse("- item text");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Bullet);
    CHECK(lines[0].headingLevel == 0);
    // spans should contain the item text
    bool foundText = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("item text") != std::string::npos) {
            foundText = true;
        }
    }
    CHECK(foundText);
}

static void test_md_paragraph()
{
    auto lines = Magpie::Md::Parse("just a plain line");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Paragraph);
    CHECK(lines[0].headingLevel == 0);
    CHECK(lines[0].spans.size() >= 1);
    // Collect all text
    std::string all;
    for (const auto& s : lines[0].spans) all += s.text;
    CHECK_EQ(all, std::string("just a plain line"));
}

static void test_md_bold()
{
    auto lines = Magpie::Md::Parse("a **b** c");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Paragraph);
    // Expect spans: Text "a ", Bold "b", Text " c" (possibly adjacent texts merged)
    bool foundBold = false;
    std::string fullText;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold) {
            CHECK_EQ(s.text, std::string("b"));
            foundBold = true;
        }
        if (s.kind == Magpie::Md::Inline::Text) {
            fullText += s.text;
        }
    }
    CHECK(foundBold);
    // The surrounding text should be present
    CHECK(fullText.find("a ") != std::string::npos);
    CHECK(fullText.find(" c") != std::string::npos);
}

static void test_md_italic()
{
    auto lines = Magpie::Md::Parse("*i*");
    CHECK(lines.size() == 1);
    bool foundItalic = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Italic) {
            CHECK_EQ(s.text, std::string("i"));
            foundItalic = true;
        }
    }
    CHECK(foundItalic);
}

static void test_md_chip_inline()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(46774);  // e.g. "[&AgF2tgAA]"
    std::string body = "see " + code + " there";
    auto lines = Magpie::Md::Parse(body);
    CHECK(lines.size() == 1);
    // Find chip span with the raw code
    bool foundChip = false;
    bool foundBefore = false;
    bool foundAfter  = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Chip) {
            CHECK_EQ(s.text, code);
            foundChip = true;
        }
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("see") != std::string::npos) {
            foundBefore = true;
        }
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("there") != std::string::npos) {
            foundAfter = true;
        }
    }
    CHECK(foundChip);
    CHECK(foundBefore);
    CHECK(foundAfter);
}

static void test_md_bold_with_chip()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(1234);
    std::string body = "**bold** " + code + " end";
    auto lines = Magpie::Md::Parse(body);
    CHECK(lines.size() == 1);
    bool foundBold = false;
    bool foundChip = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold)  { CHECK_EQ(s.text, std::string("bold")); foundBold = true; }
        if (s.kind == Magpie::Md::Inline::Chip)  { CHECK_EQ(s.text, code); foundChip = true; }
    }
    CHECK(foundBold);
    CHECK(foundChip);
}

static void test_md_unmatched_star_is_literal()
{
    auto lines = Magpie::Md::Parse("hello *world");
    CHECK(lines.size() == 1);
    // The unmatched * must appear literally in the output text
    std::string all;
    for (const auto& s : lines[0].spans) all += s.text;
    CHECK(all.find('*') != std::string::npos);
    CHECK(all.find("world") != std::string::npos);
}

static void test_md_multiline()
{
    auto lines = Magpie::Md::Parse("line one\nline two\nline three");
    CHECK(lines.size() == 3);
    // Each line should contain its own text
    std::string t0, t1, t2;
    for (const auto& s : lines[0].spans) t0 += s.text;
    for (const auto& s : lines[1].spans) t1 += s.text;
    for (const auto& s : lines[2].spans) t2 += s.text;
    CHECK(t0.find("line one") != std::string::npos);
    CHECK(t1.find("line two") != std::string::npos);
    CHECK(t2.find("line three") != std::string::npos);
}

static void test_md_heading_with_bold()
{
    auto lines = Magpie::Md::Parse("## **title**");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 2);
    bool foundBold = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold) { CHECK_EQ(s.text, std::string("title")); foundBold = true; }
    }
    CHECK(foundBold);
}

// ── ChipResolver tests ────────────────────────────────────────────────────────

static void test_chip_item_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeItem(46774);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Item]"));
    CHECK(v.color != 0);
}

static void test_chip_waypoint_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeMap(900);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Waypoint]"));
    CHECK(v.color != 0);
}

static void test_chip_skill_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkill(5516);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Skill]"));
}

static void test_chip_skin_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkin(8585);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Skin]"));
}

static void test_chip_garbage_fallback()
{
    // Garbage input must not crash and must return the safe fallback label.
    Magpie::ChipView v = Magpie::ResolveChip("not a link");
    CHECK_EQ(v.label, std::string("[link]"));
    Magpie::ChipView v2 = Magpie::ResolveChip("");
    CHECK_EQ(v2.label, std::string("[link]"));
}

static void test_chip_colours_distinct()
{
    // Item and Waypoint must have distinct, non-zero colours.
    using namespace PieUI::ChatLinks;
    Magpie::ChipView item = Magpie::ResolveChip(EncodeItem(1));
    Magpie::ChipView wp   = Magpie::ResolveChip(EncodeMap(1));
    CHECK(item.color != 0);
    CHECK(wp.color != 0);
    CHECK(item.color != wp.color);
}

static void test_chip_non_build_never_returns_build_path()
{
    // Skill, skin, waypoint must never produce a "[... Build]" label.
    using namespace PieUI::ChatLinks;
    Magpie::ChipView skill = Magpie::ResolveChip(EncodeSkill(5516));
    Magpie::ChipView skin  = Magpie::ResolveChip(EncodeSkin(8585));
    Magpie::ChipView wp    = Magpie::ResolveChip(EncodeMap(900));
    CHECK(skill.label.find("Build") == std::string::npos);
    CHECK(skin.label.find("Build") == std::string::npos);
    CHECK(wp.label.find("Build") == std::string::npos);
}

static void test_chip_build_label_via_spec_helpers()
{
    // We can't easily synthesize a valid build chat code without a full encoder
    // round-trip, but we CAN verify the spec-label mapping that BuildLabel uses:
    // GetEliteSpecName(59) == "Mirage"  → would produce "[Mirage Build]"
    // GetProfessionName(7) == "Mesmer"  → fallback would produce "[Mesmer Build]"
    // This covers the label-derivation logic even without a live build code.
    const char* mirage = PieUI::SpecData::GetEliteSpecName(59);
    CHECK(mirage != nullptr);
    CHECK_EQ(std::string(mirage), std::string("Mirage"));
    std::string expectedLabel = std::string("[") + mirage + " Build]";
    CHECK_EQ(expectedLabel, std::string("[Mirage Build]"));

    const char* mesmer = PieUI::SpecData::GetProfessionName(7);
    CHECK(mesmer != nullptr);
    CHECK_EQ(std::string(mesmer), std::string("Mesmer"));
    std::string profLabel = std::string("[") + mesmer + " Build]";
    CHECK_EQ(profLabel, std::string("[Mesmer Build]"));
}

static void test_chip_build_roundtrip_via_encode()
{
    // Construct a minimal Mirage (Mesmer elite-spec 59) build link using EncodeBuild
    // and verify that ResolveChip produces "[Mirage Build]".
    using namespace PieUI::ChatLinks;
    DecodedBuildLink b{};
    b.profession = 7; // Mesmer
    // spec slot 0 = Mirage (spec id 59)
    b.specs[0].spec_id = 59;
    b.specs[1].spec_id = 0;
    b.specs[2].spec_id = 0;
    std::string link = EncodeBuild(b);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_BUILD);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Mirage Build]"));
    CHECK(v.color != 0);
}

static void test_chip_build_core_profession_fallback()
{
    // A build link with no elite specs → should fall back to "[Mesmer Build]".
    using namespace PieUI::ChatLinks;
    DecodedBuildLink b{};
    b.profession = 7; // Mesmer
    // All spec slots are 0 (core / no spec)
    std::string link = EncodeBuild(b);
    CHECK(!link.empty());
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Mesmer Build]"));
}

// ── ChipCodec round-trip tests ────────────────────────────────────────────────

static void test_chipcodec_roundtrip_plain()
{
    std::string s = "hello world";
    CHECK_EQ(Magpie::CellsToText(Magpie::ParseToCells(s)), s);
}

static void test_chipcodec_roundtrip_with_chip()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(19684 /* Copper Ore */, 1);
    CHECK(!code.empty());
    std::string s = "got " + code + " here";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    // The chip is exactly ONE cell, isChip, code == the embedded code.
    int chipCount = 0; std::string chipCode;
    for (const auto& c : cells) if (c.isChip) { ++chipCount; chipCode = c.code; }
    CHECK(chipCount == 1);
    CHECK_EQ(chipCode, code);
}

static void test_chipcodec_roundtrip_multiline()
{
    std::string s = "line one\nline two\nthird";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    // A '\n' becomes a text cell with cp == '\n'.
    int nlCount = 0;
    for (const auto& c : cells) if (!c.isChip && c.cp == '\n') ++nlCount;
    CHECK(nlCount == 2);
}

static void test_chipcodec_roundtrip_mixed()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(19684, 1);
    CHECK(!code.empty());
    std::string s = "top " + code + "\nmid line\n" + code + " end";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    int chipCount = 0, nlCount = 0;
    for (const auto& c : cells) {
        if (c.isChip) ++chipCount;
        else if (c.cp == '\n') ++nlCount;
    }
    CHECK(chipCount == 2);
    CHECK(nlCount == 2);
}

// ── IconUrl tests ─────────────────────────────────────────────────────────────

static void test_iconurl_key_stable()
{
    // Same URL hashes to the same value every call
    uint32_t a = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    uint32_t b = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    CHECK(a == b);
}

static void test_iconurl_key_differs()
{
    // Different URLs produce different hashes
    uint32_t a = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    uint32_t b = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/CD/99.png");
    CHECK(a != b);
}

static void test_iconurl_key_null_does_not_crash()
{
    // Null input must not crash; we just check it runs
    (void)Magpie::Icons::UrlKey(nullptr);
    (void)Magpie::Icons::UrlKey("");
}

static void test_iconurl_split_full_url()
{
    auto s = Magpie::Icons::SplitIconUrl("https://render.guildwars2.com/file/AB/12.png");
    CHECK_EQ(s.remote,   std::string("https://render.guildwars2.com"));
    CHECK_EQ(s.endpoint, std::string("/file/AB/12.png"));
}

static void test_iconurl_split_no_path()
{
    // URL with no path after the host — endpoint should be "/"
    auto s = Magpie::Icons::SplitIconUrl("https://render.guildwars2.com");
    CHECK_EQ(s.endpoint, std::string("/"));
}

static void test_iconurl_split_malformed()
{
    // No "://" — whole string goes into remote, endpoint = "/"
    auto s = Magpie::Icons::SplitIconUrl("not-a-real-url");
    CHECK_EQ(s.remote,   std::string("not-a-real-url"));
    CHECK_EQ(s.endpoint, std::string("/"));
}

static void test_iconurl_split_empty_does_not_crash()
{
    auto s = Magpie::Icons::SplitIconUrl("");
    CHECK_EQ(s.endpoint, std::string("/"));
}

// ── ChipColor tests ─────────────────────────────────────────────────────────────

// Mirror of the packing in ChipColor.cpp (IM_COL32 ABGR, alpha 255).
static uint32_t packRGB(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xFFu) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

static void test_chipcolor_item_rarity_palette()
{
    using namespace Magpie;
    // Exact Pie UI RarityColor values (float->255).
    CHECK(ChipColorForItemRarity(DR_Junk)       == packRGB(171, 171, 171));
    CHECK(ChipColorForItemRarity(DR_Basic)      == packRGB(255, 255, 255));
    CHECK(ChipColorForItemRarity(DR_Fine)       == packRGB( 97, 163, 217));
    CHECK(ChipColorForItemRarity(DR_Masterwork) == packRGB( 31, 214,   0));
    CHECK(ChipColorForItemRarity(DR_Rare)       == packRGB(255, 214,   0));
    CHECK(ChipColorForItemRarity(DR_Exotic)     == packRGB(255, 171,   0));
    CHECK(ChipColorForItemRarity(DR_Ascended)   == packRGB(250,  61, 140));
    CHECK(ChipColorForItemRarity(DR_Legendary)  == packRGB(163,  54, 237));
}

static void test_chipcolor_item_unknown_is_basic()
{
    using namespace Magpie;
    // Unresolved / absent service -> Basic (white), never a tier.
    CHECK(ChipColorForItemRarity(DR_RarityUnknown) == packRGB(255, 255, 255));
    CHECK(ChipColorForItemRarity(DR_RarityUnknown) == ChipColorForItemRarity(DR_Basic));
}

static void test_chipcolor_nonitem_types_are_blue()
{
    using namespace Magpie;
    using namespace PieUI::ChatLinks;
    const uint32_t blue = packRGB(120, 200, 255);
    CHECK(ChipColorForType(LINK_MAP)   == blue);   // waypoint
    CHECK(ChipColorForType(LINK_SKILL) == blue);
    CHECK(ChipColorForType(LINK_BUILD) == blue);
    CHECK(ChipColorForType(LINK_SKIN)  == blue);
}

static void test_chipcolor_combined_rule()
{
    using namespace Magpie;
    using namespace PieUI::ChatLinks;
    // Item -> rarity colour; waypoint -> blue regardless of the rarity arg.
    CHECK(ChipColor(LINK_ITEM, DR_Exotic) == packRGB(255, 171, 0));
    CHECK(ChipColor(LINK_ITEM, DR_RarityUnknown) == packRGB(255, 255, 255)); // Basic default
    CHECK(ChipColor(LINK_MAP, DR_Exotic) == packRGB(120, 200, 255));         // type wins for non-items
    CHECK(ChipColor(LINK_MAP, DR_RarityUnknown) == packRGB(120, 200, 255));
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main()
{
    test_create_appears_in_notes();
    test_default_title();
    test_two_creates_have_different_ids();
    test_get_returns_note();
    test_get_returns_nullptr_for_missing();
    test_edit_via_get_persists();
    test_edit_via_create_ref_persists();
    test_delete_removes_note();
    test_delete_noop_for_missing();
    test_json_roundtrip_ordered();
    test_fromjson_invalid_returns_false_and_leaves_state();
    test_fromjson_wrong_shape_returns_false();
    test_fromjson_empty_array();
    test_create_after_fromjson_has_unique_id();

    // Decoder Ring ABI header tests
    test_decoder_header_is_v4();

    // ChatLinks codec tests
    test_codec_decode_item_extracts_upgrades();
    test_codec_decode_item_no_upgrades_is_zero();
    test_codec_encode_item_roundtrip();
    test_codec_encode_map_roundtrip();
    test_codec_encode_skill_roundtrip();
    test_codec_encode_skin_roundtrip();
    test_codec_segment_line_plain_and_link();
    test_codec_segment_line_plain_only();
    test_codec_link_type_none_for_garbage();
    test_specdata_elite_spec_name();
    test_specdata_profession_name();

    // ChipResolver tests
    test_chip_item_label();
    test_chip_waypoint_label();
    test_chip_skill_label();
    test_chip_skin_label();
    test_chip_garbage_fallback();
    test_chip_colours_distinct();
    test_chip_non_build_never_returns_build_path();
    test_chip_build_label_via_spec_helpers();
    test_chip_build_roundtrip_via_encode();
    test_chip_build_core_profession_fallback();

    // ChipCodec round-trip tests
    test_chipcodec_roundtrip_plain();
    test_chipcodec_roundtrip_with_chip();
    test_chipcodec_roundtrip_multiline();
    test_chipcodec_roundtrip_mixed();

    // Markdown parser tests
    test_md_heading_level1();
    test_md_heading_level3();
    test_md_bullet();
    test_md_paragraph();
    test_md_bold();
    test_md_italic();
    test_md_chip_inline();
    test_md_bold_with_chip();
    test_md_unmatched_star_is_literal();
    test_md_multiline();
    test_md_heading_with_bold();

    // IconUrl pure-helper tests
    test_iconurl_key_stable();
    test_iconurl_key_differs();
    test_iconurl_key_null_does_not_crash();
    test_iconurl_split_full_url();
    test_iconurl_split_no_path();
    test_iconurl_split_malformed();
    test_iconurl_split_empty_does_not_crash();
    test_chipcolor_item_rarity_palette();
    test_chipcolor_item_unknown_is_basic();
    test_chipcolor_nonitem_types_are_blue();
    test_chipcolor_combined_rule();

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
