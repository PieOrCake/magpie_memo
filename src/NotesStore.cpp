#include "NotesStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Magpie {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string makeId(unsigned int counter)
{
    std::ostringstream ss;
    ss << std::hex << std::setw(8) << std::setfill('0') << counter;
    return ss.str();
}

// ── NotesStore ────────────────────────────────────────────────────────────────

const std::vector<Note>& NotesStore::Notes() const
{
    return notes_;
}

Note& NotesStore::Create(const std::string& defaultTitle)
{
    Note n;
    n.id    = makeId(nextId_++);
    n.title = defaultTitle;
    notes_.push_back(std::move(n));
    return notes_.back();
}

void NotesStore::Delete(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(),
                           [&](const Note& n) { return n.id == id; });
    if (it != notes_.end())
        notes_.erase(it);
}

Note* NotesStore::Get(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(),
                           [&](const Note& n) { return n.id == id; });
    return it != notes_.end() ? &(*it) : nullptr;
}

std::string NotesStore::ToJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : notes_)
        arr.push_back({{"id", n.id}, {"title", n.title}, {"body", n.body}});
    return arr.dump();
}

bool NotesStore::FromJson(const std::string& json)
{
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return false;
    }

    if (!parsed.is_array())
        return false;

    std::vector<Note> tmp;
    tmp.reserve(parsed.size());
    for (const auto& obj : parsed) {
        if (!obj.is_object())                    return false;
        if (!obj.contains("id")    || !obj["id"].is_string())    return false;
        if (!obj.contains("title") || !obj["title"].is_string()) return false;
        if (!obj.contains("body")  || !obj["body"].is_string())  return false;
        Note n;
        n.id    = obj["id"].get<std::string>();
        n.title = obj["title"].get<std::string>();
        n.body  = obj["body"].get<std::string>();
        tmp.push_back(std::move(n));
    }

    notes_ = std::move(tmp);
    return true;
}

} // namespace Magpie
