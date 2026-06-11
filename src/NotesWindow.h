#pragma once
// NotesWindow.h — Two-pane notes UI + JSON persistence for Magpie Memo.

#include <string>
#include <filesystem>
#include "nexus/Nexus.h"
#include "NotesStore.h"

class NotesWindow {
public:
    void Init(AddonAPI_t* api);
    void Render();
    void Save();
    void Shutdown();

private:
    Magpie::NotesStore  store_;
    std::string         selectedId_;
    std::filesystem::path notesFile_;

    void selectFirst_();
    void advanceSelectionAfterDelete_(const std::string& deletedId);
};
