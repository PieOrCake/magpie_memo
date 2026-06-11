// dllmain.cpp — Magpie Memo Nexus addon lifecycle.
// Stage 2: two-pane notes shell + JSON persistence.

#include <windows.h>
#include <cstring>

#include "nexus/Nexus.h"
#include "imgui.h"
#include "Theme.h"
#include "NotesWindow.h"
#include "DecoderClient.h"
#include "IconCache.h"

// ── Version constants ─────────────────────────────────────────────────────
#define V_MAJOR    0
#define V_MINOR    1
#define V_BUILD    0
#define V_REVISION 0

// ── Globals ───────────────────────────────────────────────────────────────
AddonAPI_t*      APIDefs         = nullptr;
bool             g_WindowVisible = false;
NotesWindow      g_Notes;

// ── Forward declarations ──────────────────────────────────────────────────
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();

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
    AddonDef.Description      = "In-game sticky notes and reminders for Guild Wars 2.";
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

    // TODO: QuickAccess icon (needs an embedded icon asset)

    APIDefs->Log(LOGL_INFO, "MagpieMemo", "Magpie Memo loaded.");
}

// ── AddonUnload ───────────────────────────────────────────────────────────
void AddonUnload() {
    // Drop Decoder Ring subscriptions + cache before APIDefs goes away.
    Magpie::Decoder::Shutdown();

    // Save notes before we lose APIDefs (Shutdown only writes to disk — safe).
    g_Notes.Shutdown();

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
}

// ── ProcessKeybind ────────────────────────────────────────────────────────
void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_MAGPIE_MEMO_TOGGLE") == 0) {
        g_WindowVisible = !g_WindowVisible;
    }
}
