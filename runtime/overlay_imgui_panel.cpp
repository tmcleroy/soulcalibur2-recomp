/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The overlay's look and layout in Dear ImGui: the runtime's panel (the free camera's, online play's) under the life
   bars, its hint along the bottom, and Soulcalibur II's training panels (sc2_panel.h) down each player's side: the
   combo readout under the life bar, the input display below it. A note for the player goes above the hint. */
#include "overlay_imgui_panel.h"
#include "sc2_panel.h"
#include "sc2_touch.h"
#include "d3d_vblank.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>

namespace {

/* Text is sized for the target's height, as the game's own is: ImGui's
   13-pixel font at kReferenceHeight lines, built once at that size. */
constexpr float kReferenceHeight = 480.0f;
constexpr float kFontPixels = 13.0f;
float panel_scale = 1.0f;

constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav;

/* One player's side: x at the picture's edge (pivot 0 on the left, 1 on the right) */
void Side(const Sc2Panel &panel, int player, float width, float height)
{
    const float x = player == 0 ? width * 0.02f : width * 0.98f, pivot = player == 0 ? 0.0f : 1.0f;
    char name[32];
    if (panel.readout_rows[player] != 0u) {
        ImGui::SetNextWindowPos(ImVec2(x, height * 0.27f), 0, ImVec2(pivot, 0.0f));
        snprintf(name, sizeof name, "sc2-readout-%d", player);
        ImGui::Begin(name, nullptr, kFlags);
        for (unsigned i = 0u; i < panel.readout_rows[player]; ++i) {
            if (i == 0u) ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), "%s", panel.readout[player][i]);
            else ImGui::TextUnformatted(panel.readout[player][i]);
        }
        ImGui::End();
    }
    if (panel.input_rows[player] != 0u) {
        ImGui::SetNextWindowPos(ImVec2(x, height * 0.46f), 0, ImVec2(pivot, 0.0f));
        /* (a fixed width, so the box keeps its size as the rows change) */
        const float fixed = ImGui::CalcTextSize(" 99  9  A+B+K+G  ").x + ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImGui::SetNextWindowSizeConstraints(ImVec2(fixed, 0.0f), ImVec2(fixed, height));
        snprintf(name, sizeof name, "sc2-input-%d", player);
        ImGui::Begin(name, nullptr, kFlags);
        for (unsigned i = 0u; i < panel.input_rows[player]; ++i) {
            if (i == 0u) ImGui::TextUnformatted(panel.input[player][i]);
            else ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.82f, 1.0f), "%s", panel.input[player][i]);
        }
        ImGui::End();
    }
}

/* The hit boxes, over the picture (the game's 640x480 frame, kept to its aspect in the window or stretched over it,
   as the presenter places it), under the panels */
void Shapes(const Sc2Panel &panel, float width, float height)
{
    if (panel.shape_count == 0u) return;
    static const float aspect = std::getenv("RECOMP_D3D_WIDESCREEN") ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = width, h = height;
    if (recomp_d3d_present_stretch) {}
    else if (width / height > aspect) w = height * aspect;
    else h = width / aspect;
    const float left = (width - w) * 0.5f, top = (height - h) * 0.5f, sx = w / 640.0f, sy = h / 480.0f;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const float thick = 1.5f * panel_scale;
    for (unsigned i = 0u; i < panel.shape_count && i < SC2_PANEL_SHAPES; ++i) {
        const Sc2PanelShape &shape = panel.shapes[i];
        const ImVec2 a(left + shape.x0 * sx, top + shape.y0 * sy), b(left + shape.x1 * sx, top + shape.y1 * sy);
        switch (shape.kind) {
        case SC2_SHAPE_HURT:
            draw->AddCircleFilled(a, shape.radius * sy, IM_COL32(60, 140, 255, 40));
            draw->AddCircle(a, shape.radius * sy, IM_COL32(90, 170, 255, 200), 0, thick);
            break;
        case SC2_SHAPE_PUSH:
            draw->AddCircle(a, shape.radius * sy, IM_COL32(80, 230, 110, 150), 0, thick);
            break;
        case SC2_SHAPE_ATTACK:
            draw->AddLine(a, b, IM_COL32(255, 190, 60, 170), thick * 2.0f);
            break;
        default:
            draw->AddLine(a, b, IM_COL32(255, 50, 40, 255), thick * 3.0f);
            draw->AddCircleFilled(b, thick * 2.5f, IM_COL32(255, 50, 40, 255));
            break;
        }
    }
}

/* The frame meter: a row of cells for each fighter above the bottom of the picture, a frame a cell, the newest
   on the right: free (grey), startup (green), active (red), recovery (blue), hit or guard stun (yellow) */
void Meter(const Sc2Panel &panel, float width, float height)
{
    if (panel.meter_length == 0u) return;
    static const ImU32 kColours[] = {IM_COL32(0, 0, 0, 0), IM_COL32(70, 70, 80, 200), IM_COL32(60, 200, 90, 240),
        IM_COL32(240, 60, 50, 255), IM_COL32(60, 120, 240, 240), IM_COL32(240, 200, 50, 240)};
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const float cell = width * 0.9f / SC2_PANEL_METER, rows = cell * 1.6f;
    const float left = width * 0.05f, top = height * 0.80f;
    draw->AddRectFilled(ImVec2(left - 3.0f, top - 3.0f), ImVec2(left + cell * SC2_PANEL_METER + 3.0f, top + rows * 2.0f + 5.0f),
        IM_COL32(5, 15, 25, 170), 3.0f);
    for (int p = 0; p < 2; ++p) {
        const float y = top + p * (rows + 2.0f);
        for (unsigned i = 0u; i < panel.meter_length && i < SC2_PANEL_METER; ++i) {
            const unsigned kind = panel.meter[p][i] < 6u ? panel.meter[p][i] : 0u;
            if (kind == SC2_METER_NONE) continue;
            const float x = left + (SC2_PANEL_METER - panel.meter_length + i) * cell;
            draw->AddRectFilled(ImVec2(x + 0.5f, y), ImVec2(x + cell - 0.5f, y + rows), kColours[kind]);
        }
    }
}

}  // namespace

void recomp_overlay_imgui_style(float target_height)
{
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    panel_scale = target_height > kReferenceHeight ? target_height / kReferenceHeight : 1.0f;
    ImFontConfig font{};
    font.SizePixels = kFontPixels * panel_scale;
    io.Fonts->AddFontDefault(&font);
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.06f, 0.10f, 0.70f);
    style.ScaleAllSizes(panel_scale);
}

void recomp_overlay_imgui_build(const RecompOverlayText &text, float width, float height)
{
    Sc2Panel panel;
    sc2_panel_take(&panel);
    sc2_touch_draw(width, height);
    if (sc2_touch_menu()) return; /* (over the in-game menu: the touch controls alone) */
    Shapes(panel, width, height);
    Meter(panel, width, height);
    Side(panel, 0, width, height);
    Side(panel, 1, width, height);
    if (panel.fps[0] != '\0') {
        ImGui::SetNextWindowPos(ImVec2(width * 0.98f, height * 0.02f), 0, ImVec2(1.0f, 0.0f));
        ImGui::Begin("sc2-fps", nullptr, kFlags);
        ImGui::TextUnformatted(panel.fps);
        ImGui::End();
    }
    if (panel.note[0] != '\0') {
        ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * (text.hint[0] != '\0' ? 0.86f : 0.95f)), 0, ImVec2(0.5f, 1.0f));
        ImGui::Begin("sc2-note", nullptr, kFlags);
        ImGui::TextUnformatted(panel.note);
        ImGui::End();
    }
    if (text.title[0] != '\0' || text.count != 0u) {
        /* A small box under the life bars, centred: the title, then one line per entry. */
        ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.17f), 0, ImVec2(0.5f, 0.0f));
        ImGui::Begin("recomp-overlay-panel", nullptr, kFlags);
        if (text.title[0] != '\0') ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), "%s", text.title);
        for (unsigned i = 0u; i < text.count; ++i) ImGui::TextUnformatted(text.line[i]);
        ImGui::End();
    }
    /* The hint: the controls, along the bottom. */
    if (text.hint[0] == '\0') return;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.97f), 0, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(width * 0.96f, height));
    ImGui::Begin("recomp-overlay-hint", nullptr, kFlags);
    ImGui::PushTextWrapPos(width * 0.94f);
    ImGui::TextUnformatted(text.hint);
    ImGui::PopTextWrapPos();
    ImGui::End();
}
