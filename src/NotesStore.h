#pragma once
#include <string>
#include <vector>

namespace Magpie {

struct Note {
    std::string id;
    std::string title;
    std::string body;
};

class NotesStore {
public:
    const std::vector<Note>& Notes() const;

    Note& Create(const std::string& defaultTitle = "Untitled note");
    void  Delete(const std::string& id);
    Note* Get(const std::string& id);

    std::string ToJson() const;
    bool        FromJson(const std::string& json);

private:
    std::vector<Note> notes_;
    unsigned int      nextId_ = 0;
};

} // namespace Magpie
