#include "NotesStore.h"
#include "chat/ChatLinks.h"
#include "chat/SpecData.h"
#include <iostream>
#include <string>

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

// ── ChatLinks codec tests ─────────────────────────────────────────────────────

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

    // ChatLinks codec tests
    test_codec_encode_item_roundtrip();
    test_codec_encode_map_roundtrip();
    test_codec_encode_skill_roundtrip();
    test_codec_encode_skin_roundtrip();
    test_codec_segment_line_plain_and_link();
    test_codec_segment_line_plain_only();
    test_codec_link_type_none_for_garbage();
    test_specdata_elite_spec_name();
    test_specdata_profession_name();

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
