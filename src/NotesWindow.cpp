// NotesWindow.cpp — Two-pane notes UI + JSON persistence for Magpie Memo.

#include "NotesWindow.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "Theme.h"
#include "Markdown.h"
#include "MarkdownRender.h"

namespace fs = std::filesystem;

// ── Init ──────────────────────────────────────────────────────────────────

void NotesWindow::Init(AddonAPI_t* api)
{
    api_ = api;

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
    // The to-be-deleted note is still in the list at this point. Pick the
    // successor if there is one, otherwise the predecessor, otherwise (it was
    // the only note) clear the selection.
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].id == deletedId) {
            if (i + 1 < notes.size())      selectedId_ = notes[i + 1].id;  // successor
            else if (i > 0)                selectedId_ = notes[i - 1].id;  // predecessor
            else                           selectedId_.clear();             // sole note
            return;
        }
    }
    // Deleted id not found in list; fall back to first.
    selectFirst_();
}

void NotesWindow::enterEdit_(Magpie::Note* note)
{
    if (!note) return;
    bodyEditor_.SetText(note->body);
    titleBuffer_ = note->title;
    mode_ = Mode::Edit;
}

void NotesWindow::commitEdit_()
{
    Magpie::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);
    if (note) {
        note->body  = bodyEditor_.GetText();
        note->title = titleBuffer_;
        Save();
    }
    mode_ = Mode::View;
    bodyEditor_.Clear();
    titleBuffer_.clear();
}

void NotesWindow::cancelEdit_()
{
    mode_ = Mode::View;
    bodyEditor_.Clear();
    titleBuffer_.clear();
}

void NotesWindow::resolvePending_()
{
    if (pendingCreate_) {
        pendingCreate_ = false;
        pendingSelectId_.clear();
        // Create the new note, select it, and open in Edit mode.
        Magpie::Note& n = store_.Create("Untitled note");
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    } else if (!pendingSelectId_.empty()) {
        selectedId_ = pendingSelectId_;
        pendingSelectId_.clear();
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestSelect_(const std::string& id)
{
    if (id == selectedId_) return;  // no-op

    bool dirty = (mode_ == Mode::Edit);
    if (dirty) {
        Magpie::Note* cur = store_.Get(selectedId_);
        if (cur) {
            dirty = (bodyEditor_.GetText() != cur->body) || (titleBuffer_ != cur->title);
        }
    }

    if (dirty) {
        pendingSelectId_ = id;
        pendingCreate_   = false;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        selectedId_ = id;
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestCreate_()
{
    bool dirty = (mode_ == Mode::Edit);
    if (dirty) {
        Magpie::Note* cur = store_.Get(selectedId_);
        if (cur) {
            dirty = (bodyEditor_.GetText() != cur->body) || (titleBuffer_ != cur->title);
        }
    }

    if (dirty) {
        pendingCreate_   = true;
        pendingSelectId_.clear();
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        Magpie::Note& n = store_.Create("Untitled note");
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    }
}

// ── Unsaved-changes modal ─────────────────────────────────────────────────

void NotesWindow::renderUnsavedModal_()
{
    // Centre the popup on screen.
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 centre  = ImVec2(display.x * 0.5f, display.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This note has unsaved changes.");
        ImGui::Spacing();

        Theme::PushGreenButton();
        if (ImGui::Button("Save", ImVec2(80, 0))) {
            commitEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Discard", ImVec2(80, 0))) {
            cancelEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            pendingSelectId_.clear();
            pendingCreate_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ── Render ────────────────────────────────────────────────────────────────

void NotesWindow::Render()
{
    // Draw the modal first so it can open this frame if triggered below.
    renderUnsavedModal_();

    const auto& notes = store_.Notes();

    // ── Left pane ─────────────────────────────────────────────────────────
    const float leftWidth = 200.0f;

    // Reserve space at the bottom for the Create button.
    const float btnAreaHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImVec2 leftPaneSize(leftWidth, ImGui::GetContentRegionAvail().y - btnAreaHeight);

    Theme::PushDarkFrameBg();
    ImGui::BeginChild("##notes_list", leftPaneSize, true);
    Theme::PopFrameBg();

    // Capture a click here but defer the actual navigation until AFTER EndChild:
    // OpenPopup() (inside requestSelect_) hashes its id against the current
    // window, so it must run in the SAME window scope as the BeginPopupModal in
    // renderUnsavedModal_ (the parent window) — not inside this child — or the
    // unsaved-changes modal never opens.
    std::string clickedId;
    for (const auto& note : notes) {
        bool selected = (note.id == selectedId_);
        if (ImGui::Selectable(note.title.c_str(), selected)) {
            clickedId = note.id;
        }
    }

    ImGui::EndChild();

    if (!clickedId.empty()) {
        requestSelect_(clickedId);
    }

    // Create New Note button sits below the list.
    Theme::PushGreenButton();
    if (ImGui::Button("Create New Note", ImVec2(leftWidth, 0))) {
        requestCreate_();
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
    } else if (mode_ == Mode::Edit) {
        // ── Edit mode ──────────────────────────────────────────────────────

        // Save / Cancel buttons at the top.
        Theme::PushGreenButton();
        if (ImGui::Button("Save", ImVec2(70, 0))) {
            commitEdit_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) {
            cancelEdit_();
        }
        Theme::PopButton();

        // Delete button on the far right, same row.
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - 60.0f);
        }
        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(60.0f, 0))) {
            std::string idToDelete = note->id;
            cancelEdit_();
            advanceSelectionAfterDelete_(idToDelete);
            store_.Delete(idToDelete);
            Save();
            note = nullptr;
        }
        Theme::PopButton();

        ImGui::Separator();

        if (note != nullptr) {
            // Editable title field.
            Theme::PushEditBg();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##title", &titleBuffer_);
            Theme::PopFrameBg();

            ImGui::Spacing();

            // Editable body — chat codes render as atomic chip cells;
            // markdown syntax stays as raw text (edit = source view).
            bodyEditor_.Render("##body_edit",
                               ImGui::GetContentRegionAvail().x,
                               ImGui::GetColorU32(Theme::BORDER_GOLD),
                               /*minLines=*/6);
        }
    } else {
        // ── View mode ──────────────────────────────────────────────────────

        // Title row with Edit and Delete buttons on the right.
        ImGui::Text("%s", note->title.c_str());

        // Edit button right-aligned before Delete.
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - 60.0f - 65.0f - ImGui::GetStyle().ItemSpacing.x);
        }
        Theme::PushGreenButton();
        if (ImGui::Button("Edit", ImVec2(60.0f, 0))) {
            enterEdit_(note);
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(60.0f, 0))) {
            std::string idToDelete = note->id;
            advanceSelectionAfterDelete_(idToDelete);
            store_.Delete(idToDelete);
            Save();
            note = nullptr;
        }
        Theme::PopButton();

        ImGui::Separator();

        if (note != nullptr) {
            // Rendered markdown preview inside a scrollable child so long
            // notes scroll. EDIT mode shows the raw source; VIEW shows this.
            ImFont* fontBig = nullptr;
            if (api_ != nullptr) {
                auto* nl = static_cast<NexusLinkData_t*>(
                    api_->DataLink_Get(DL_NEXUS_LINK));
                if (nl != nullptr) fontBig = static_cast<ImFont*>(nl->FontBig);
            }

            ImGui::BeginChild("##view_body", ImVec2(0, 0), false);
            const std::vector<Magpie::Md::Line> lines = Magpie::Md::Parse(note->body);
            Magpie::RenderMarkdown(lines, fontBig, ImGui::GetContentRegionAvail().x);
            ImGui::EndChild();
        }
    }

    ImGui::EndChild();
}
