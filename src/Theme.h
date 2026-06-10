#pragma once
// Theme.h — Magpie Memo accent palette + RAII push/pop helpers.
// Inherits Nexus's default ImGui style; applies the PieOrCake suite's
// consistent accent colours per-widget (no global ApplyTheme call needed).

#include "imgui.h"

namespace Theme {

// ── Gold / amber window border ────────────────────────────────────────────
// ImVec4 lacks a constexpr constructor in this ImGui version; use inline const.
inline const ImVec4 BORDER_GOLD        = { 0.90f, 0.75f, 0.25f, 0.90f };
constexpr float     BORDER_SIZE        = 2.0f;

// ── Green action-button set ───────────────────────────────────────────────
inline const ImVec4 BTN_GREEN          = { 0.18f, 0.45f, 0.18f, 0.90f };
inline const ImVec4 BTN_GREEN_HOVERED  = { 0.25f, 0.60f, 0.25f, 0.95f };
inline const ImVec4 BTN_GREEN_ACTIVE   = { 0.30f, 0.70f, 0.30f, 1.00f };

// ── Amber secondary-button set ────────────────────────────────────────────
inline const ImVec4 BTN_AMBER          = { 0.22f, 0.20f, 0.12f, 0.80f };
inline const ImVec4 BTN_AMBER_HOVERED  = { 0.35f, 0.30f, 0.14f, 0.90f };
inline const ImVec4 BTN_AMBER_ACTIVE   = { 0.45f, 0.38f, 0.16f, 1.00f };

// ── Dark frame background ─────────────────────────────────────────────────
inline const ImVec4 FRAME_BG_DARK      = { 0.10f, 0.10f, 0.13f, 1.00f };

// ── Scoped helpers ────────────────────────────────────────────────────────
// Push the gold window border style. Call PopWindowBorder() after ImGui::End().
inline void PushWindowBorder() {
    ImGui::PushStyleColor(ImGuiCol_Border, BORDER_GOLD);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, BORDER_SIZE);
}
inline void PopWindowBorder() {
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);
}

// Push green action-button colours. Call PopButton() after the button widget.
inline void PushGreenButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_GREEN);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_GREEN_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_GREEN_ACTIVE);
}
inline void PopButton() {
    ImGui::PopStyleColor(3);
}

// Push amber secondary-button colours. Call PopButton() after the button widget.
inline void PushAmberButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_AMBER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_AMBER_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_AMBER_ACTIVE);
}

// Push dark frame background (e.g. for text input fields).
// Call PopFrameBg() after the widget.
inline void PushDarkFrameBg() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FRAME_BG_DARK);
}
inline void PopFrameBg() {
    ImGui::PopStyleColor(1);
}

} // namespace Theme
