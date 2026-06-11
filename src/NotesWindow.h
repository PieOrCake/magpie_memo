#pragma once
// NotesWindow.h — Two-pane notes UI + JSON persistence for Magpie Memo.

#include <string>
#include <filesystem>
#include "nexus/Nexus.h"
#include "NotesStore.h"
#include "ChipTextEdit.h"

class NotesWindow {
public:
    void Init(AddonAPI_t* api);
    void Render();
    void Save();
    void Shutdown();

private:
    // ── Mode ──────────────────────────────────────────────────────────────
    enum class Mode { View, Edit };

    // ── Persistent state ──────────────────────────────────────────────────
    AddonAPI_t*           api_ = nullptr;  // Nexus API (for FontBig via DataLink)
    Magpie::NotesStore    store_;
    std::string           selectedId_;
    std::filesystem::path notesFile_;

    // ── Edit-mode working copies ──────────────────────────────────────────
    Mode                   mode_        = Mode::View;
    Magpie::ChipTextEdit   bodyEditor_;  // working copy of note body while editing
    std::string            titleBuffer_; // working copy of note title while editing

    // ── Dirty-guard pending navigation ────────────────────────────────────
    // When a navigation is requested while dirty we stash it here and open
    // the "Unsaved Changes" modal.  Both fields are cleared after resolution.
    std::string pendingSelectId_;  // non-empty => switch to this note after save/discard
    bool        pendingCreate_ = false;  // true => create new note after save/discard

    // ── Delete-confirmation ───────────────────────────────────────────────
    std::string pendingDeleteId_;  // non-empty => the note awaiting delete confirmation

    // ── Private helpers ───────────────────────────────────────────────────
    void selectFirst_();
    void advanceSelectionAfterDelete_(const std::string& deletedId);

    // Enter edit mode for the currently selected note (seeds buffers).
    void enterEdit_(Magpie::Note* note);

    // Commit edit buffers to store and persist.
    void commitEdit_();

    // Discard edit buffers and return to View.
    void cancelEdit_();

    // Perform the stashed pending navigation after save/discard.
    void resolvePending_();

    // Request navigation: intercepts with modal if dirty, else navigates directly.
    void requestSelect_(const std::string& id);
    void requestCreate_();

    // Draw the dirty-state modal.  Returns true when open (caller must not
    // do further navigation this frame).
    void renderUnsavedModal_();

    // Draw the delete-confirmation modal (acts on pendingDeleteId_).
    void renderDeleteModal_();

    // Perform the actual deletion of `id` (handles edit-mode + selection).
    void performDelete_(const std::string& id);
};
