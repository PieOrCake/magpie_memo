#include "NotesStore.h"
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

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
