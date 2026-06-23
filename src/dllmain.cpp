// dllmain.cpp — Magpie Memo Nexus addon lifecycle.
// Stage 2: two-pane notes shell + JSON persistence.

#include <windows.h>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>

#include "nexus/Nexus.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include "Theme.h"
#include "NotesWindow.h"
#include "DecoderClient.h"
#include "IconCache.h"
#include "EmbeddedIcon.h"   // MAGPIE_ICON[] + MAGPIE_ICON_size (generated from icon.png)

// ── Quick-access identifiers ──────────────────────────────────────────────
#define QA_ID    "QA_MAGPIE_MEMO"
#define TEX_ICON "TEX_MAGPIE_MEMO_ICON"

// ── Version constants ─────────────────────────────────────────────────────
#define V_MAJOR    0
#define V_MINOR    9
#define V_BUILD    2
#define V_REVISION 0

// ── Globals ───────────────────────────────────────────────────────────────
AddonAPI_t*      APIDefs         = nullptr;
bool             g_WindowVisible = false;
bool             g_ShowQAIcon    = true;   // show the quick-access icon (persisted; default on)
bool             g_QAIconAdded   = false;  // tracks whether the shortcut is currently registered
NotesWindow      g_Notes;

// ── Forward declarations ──────────────────────────────────────────────────
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();

// ── Settings persistence (settings.json in the addon data dir) ────────────
static std::string SettingsPath() {
    if (!APIDefs) return std::string();
    const char* dir = APIDefs->Paths_GetAddonDirectory("MagpieMemo");
    if (!dir || !*dir) return std::string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (std::filesystem::path(dir) / "settings.json").string();
}

static void SaveSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    nlohmann::json j;
    j["show_qa_icon"] = g_ShowQAIcon;
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (f.is_open()) f << j.dump(2);
}

static void LoadSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    nlohmann::json j;
    try { j = nlohmann::json::parse(f); }
    catch (...) { return; }   // malformed: keep defaults
    if (j.contains("show_qa_icon") && j["show_qa_icon"].is_boolean())
        g_ShowQAIcon = j["show_qa_icon"].get<bool>();
}

// Add/remove the quick-access shortcut, keeping g_QAIconAdded in sync. The icon
// texture is loaded once at AddonLoad regardless; these only toggle the shortcut.
static void AddQAIcon() {
    if (!APIDefs || g_QAIconAdded) return;
    APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON, "KB_MAGPIE_MEMO_TOGGLE", "Magpie Memo");
    g_QAIconAdded = true;
}
static void RemoveQAIcon() {
    if (!APIDefs || !g_QAIconAdded) return;
    APIDefs->QuickAccess_Remove(QA_ID);
    g_QAIconAdded = false;
}

// ── Static addon definition ───────────────────────────────────────────────
static AddonDefinition_t AddonDef = {};

// ── DllMain ───────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD /*ul_reason*/, LPVOID /*lpReserved*/) {
    return TRUE;
}

// ── GetAddonDef (exported) ────────────────────────────────────────────────
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature        = 0x4D61704D;          // "MapM" — distinct from Alter Ego 0xA17E3E90
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = "Magpie Memo";
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author           = "PieOrCake.7635";
    AddonDef.Description      = "Notepad with markdown and rich chips. Recommended: Decoder Ring.";
    AddonDef.Load             = AddonLoad;
    AddonDef.Unload           = AddonUnload;
    AddonDef.Flags            = AF_None;
    AddonDef.Provider         = UP_GitHub;
    AddonDef.UpdateLink       = "https://github.com/PieOrCake/magpie_memo";

    return &AddonDef;
}

// ── AddonLoad ─────────────────────────────────────────────────────────────
// TODO: register a WndProc via APIDefs->WndProc_Register to call
//       Magpie::ChipInputActive / Magpie::RequestChipBlur for held-key edge
//       cases (e.g. WASD leaking to the game while a chip cell is focused).
//       This is deferred polish -- not required for the basic integration.
void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;

    // Share Nexus's ImGui context and allocators with this DLL's copy of ImGui.
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree);

    // Register render callbacks.
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    // Register toggle keybind (default CTRL+SHIFT+M).
    APIDefs->InputBinds_RegisterWithString("KB_MAGPIE_MEMO_TOGGLE", ProcessKeybind, "CTRL+SHIFT+M");

    // Close window on Escape.
    APIDefs->GUI_RegisterCloseOnEscape("Magpie Memo", &g_WindowVisible);

    // Initialise notes store (loads from disk).
    g_Notes.Init(APIDefs);

    // Icon cache (async chip-icon download/upload via Nexus textures).
    Magpie::Icons::Init(APIDefs);

    // Decoder Ring consumer client (optional provider; lifetime-contract safe).
    Magpie::Decoder::Init(APIDefs);

    // Load persisted settings (e.g. the quick-access toggle), then register the
    // quick-access icon from the embedded icon.png (same method as Alter Ego).
    LoadSettings();
    APIDefs->Textures_LoadFromMemory(TEX_ICON, (void*)MAGPIE_ICON, MAGPIE_ICON_size, nullptr);
    if (g_ShowQAIcon) AddQAIcon();

    APIDefs->Log(LOGL_INFO, "MagpieMemo", "Magpie Memo loaded.");
}

// ── AddonUnload ───────────────────────────────────────────────────────────
void AddonUnload() {
    // Drop Decoder Ring subscriptions + cache before APIDefs goes away.
    Magpie::Decoder::Shutdown();

    // Mark the icon cache dead so a late texture-load callback can't touch torn-down state.
    Magpie::Icons::Shutdown();

    // Save notes before we lose APIDefs (Shutdown only writes to disk — safe).
    g_Notes.Shutdown();

    // Remove the quick-access shortcut if it was registered.
    RemoveQAIcon();

    APIDefs->GUI_DeregisterCloseOnEscape("Magpie Memo");
    APIDefs->InputBinds_Deregister("KB_MAGPIE_MEMO_TOGGLE");
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    APIDefs = nullptr;
}

// ── AddonRender ───────────────────────────────────────────────────────────
void AddonRender() {
    if (!g_WindowVisible) return;

    Theme::PushWindowBorder();

    if (ImGui::Begin("Magpie Memo", &g_WindowVisible)) {
        g_Notes.Render();
    }
    ImGui::End();

    Theme::PopWindowBorder();
}

// ── AddonOptions ──────────────────────────────────────────────────────────
void AddonOptions() {
    // Live Decoder Ring presence — re-evaluated every frame, never latched.
    // Drives the chip experience: full names + icons + tooltips when present,
    // basic labels (still fully usable) when absent.
    if (Magpie::Decoder::Present()) {
        ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.50f, 1.0f),
            "Decoder Ring: connected -- chips show full names and icons.");
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.78f, 0.45f, 1.0f),
            "Decoder Ring not loaded -- chips show basic labels.");
        ImGui::TextDisabled("Copy code and Open in wiki still work.");
    }

    ImGui::Separator();

    // Quick-access icon toggle (persisted; default on).
    if (ImGui::Checkbox("Show quick access icon", &g_ShowQAIcon)) {
        if (g_ShowQAIcon) AddQAIcon();
        else              RemoveQAIcon();
        SaveSettings();
    }
}

// ── ProcessKeybind ────────────────────────────────────────────────────────
void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_MAGPIE_MEMO_TOGGLE") == 0) {
        g_WindowVisible = !g_WindowVisible;
    }
}
