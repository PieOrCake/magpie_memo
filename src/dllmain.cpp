// dllmain.cpp — Magpie Memo Nexus addon lifecycle skeleton.
// Stage 1: lifecycle registration, empty themed window, options placeholder.

#include <windows.h>
#include <cstring>

#include "nexus/Nexus.h"
#include "imgui.h"
#include "Theme.h"

// ── Version constants ─────────────────────────────────────────────────────
#define V_MAJOR    0
#define V_MINOR    1
#define V_BUILD    0
#define V_REVISION 0

// ── Globals ───────────────────────────────────────────────────────────────
AddonAPI_t*      APIDefs         = nullptr;
bool             g_WindowVisible = false;

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

    // TODO: QuickAccess icon (needs an embedded icon asset)

    APIDefs->Log(LOGL_INFO, "MagpieMemo", "Magpie Memo loaded.");
}

// ── AddonUnload ───────────────────────────────────────────────────────────
void AddonUnload() {
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
        ImGui::TextDisabled("[ Magpie Memo - notes go here ]");
    }
    ImGui::End();

    Theme::PopWindowBorder();
}

// ── AddonOptions ──────────────────────────────────────────────────────────
void AddonOptions() {
    ImGui::TextDisabled("Magpie Memo options will appear here.");
}

// ── ProcessKeybind ────────────────────────────────────────────────────────
void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_MAGPIE_MEMO_TOGGLE") == 0) {
        g_WindowVisible = !g_WindowVisible;
    }
}
