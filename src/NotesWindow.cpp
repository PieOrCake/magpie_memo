// NotesWindow.cpp — Two-pane notes UI + JSON persistence for Magpie Memo.

#include "NotesWindow.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "imgui.h"
#include "Theme.h"

namespace fs = std::filesystem;

// ── Init ──────────────────────────────────────────────────────────────────

void NotesWindow::Init(AddonAPI_t* api)
{
    const char* dir = api->Paths_GetAddonDirectory("Magpie Memo");
    if (dir && dir[0] != '\0') {
        fs::path addonDir(dir);
        std::error_code ec;
        fs::create_directories(addonDir, ec);   // no-op if already exists
        notesFile_ = addonDir / "notes.json";
    }

    if (!notesFile_.empty() && fs::exists(notesFile_)) {
        std::ifstream f(notesFile_);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            store_.FromJson(ss.str());  // rolls back silently on bad JSON
        }
    }

    selectFirst_();
}

// ── Save / Shutdown ───────────────────────────────────────────────────────

void NotesWindow::Save()
{
    if (notesFile_.empty()) return;

    std::ofstream f(notesFile_, std::ios::out | std::ios::trunc);
    if (f.is_open())
        f << store_.ToJson();
}

void NotesWindow::Shutdown()
{
    Save();
}

// ── Internal helpers ──────────────────────────────────────────────────────

void NotesWindow::selectFirst_()
{
    const auto& notes = store_.Notes();
    selectedId_ = notes.empty() ? std::string{} : notes.front().id;
}

void NotesWindow::advanceSelectionAfterDelete_(const std::string& deletedId)
{
    const auto& notes = store_.Notes();
    if (notes.empty()) {
        selectedId_.clear();
        return;
    }
    // Try to keep the same index position; fall back to last note.
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].id == deletedId) {
            // This note hasn't been deleted yet — pick successor or predecessor.
            selectedId_ = (i + 1 < notes.size()) ? notes[i + 1].id : notes[i - 1 < notes.size() ? i - 1 : 0].id;
            return;
        }
    }
    // Deleted id not found in list; fall back to first.
    selectFirst_();
}

// ── Render ────────────────────────────────────────────────────────────────

void NotesWindow::Render()
{
    const auto& notes = store_.Notes();

    // ── Left pane ─────────────────────────────────────────────────────────
    const float leftWidth = 200.0f;

    // Reserve space at the bottom for the Create button.
    const float btnAreaHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImVec2 leftPaneSize(leftWidth, ImGui::GetContentRegionAvail().y - btnAreaHeight);

    Theme::PushDarkFrameBg();
    ImGui::BeginChild("##notes_list", leftPaneSize, true);
    Theme::PopFrameBg();

    for (const auto& note : notes) {
        bool selected = (note.id == selectedId_);
        if (ImGui::Selectable(note.title.c_str(), selected)) {
            selectedId_ = note.id;
        }
    }

    ImGui::EndChild();

    // Create New Note button sits below the list.
    Theme::PushGreenButton();
    if (ImGui::Button("Create New Note", ImVec2(leftWidth, 0))) {
        Magpie::Note& n = store_.Create("Untitled note");
        selectedId_ = n.id;
        Save();
    }
    Theme::PopButton();

    // ── Right pane ────────────────────────────────────────────────────────
    ImGui::SameLine();

    ImGui::BeginChild("##notes_detail", ImVec2(0, 0), false);

    Magpie::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);

    if (note == nullptr) {
        // Empty state — light magpie flavour, all ASCII.
        ImGui::Spacing();
        ImGui::TextDisabled("Your nest is empty.");
        ImGui::TextDisabled("Tap 'Create New Note' to start collecting.");
    } else {
        // Title row with Delete button on the right.
        ImGui::Text("%s", note->title.c_str());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x - 60.0f);
        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(60.0f, 0))) {
            std::string idToDelete = note->id;           // copy before erase
            advanceSelectionAfterDelete_(idToDelete);
            store_.Delete(idToDelete);
            Save();
            note = nullptr;
        }
        Theme::PopButton();

        ImGui::Separator();

        if (note != nullptr) {
            // Read-only body; InputTextMultiline gives a scrollable view.
            ImVec2 bodySize(ImGui::GetContentRegionAvail().x,
                            ImGui::GetContentRegionAvail().y);
            Theme::PushDarkFrameBg();
            ImGui::InputTextMultiline(
                "##body",
                const_cast<char*>(note->body.c_str()),
                note->body.size() + 1,
                bodySize,
                ImGuiInputTextFlags_ReadOnly);
            Theme::PopFrameBg();
        }
    }

    ImGui::EndChild();
}
