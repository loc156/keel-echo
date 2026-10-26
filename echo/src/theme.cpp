#include "echo/theme.h"

namespace echo {
namespace theme {

namespace {

float g_fontScale = 1.0f;
bool g_dark = true;
ImVec4 g_accent = ImVec4(0.30f, 0.56f, 0.95f, 1.00f);

ImVec4 shade(const ImVec4& color, float factor, float alpha = -1.0f)
{
    ImVec4 out(color.x * factor, color.y * factor, color.z * factor,
               alpha < 0.0f ? color.w : alpha);
    return out;
}

void apply()
{
    ImGuiStyle style; // fresh defaults, so repeat calls never accumulate

    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    if (g_dark) {
        ImGui::StyleColorsDark(&style);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.08f, 0.09f, 0.60f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.13f, 0.98f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.47f, 0.50f, 1.00f);
    } else {
        ImGui::StyleColorsLight(&style);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.58f, 1.00f);
    }

    ImVec4* c = style.Colors;
    c[ImGuiCol_Button] = shade(g_accent, 0.55f, 0.90f);
    c[ImGuiCol_ButtonHovered] = shade(g_accent, 0.75f, 1.00f);
    c[ImGuiCol_ButtonActive] = g_accent;
    c[ImGuiCol_Header] = shade(g_accent, 0.55f, 0.55f);
    c[ImGuiCol_HeaderHovered] = shade(g_accent, 0.75f, 0.70f);
    c[ImGuiCol_HeaderActive] = shade(g_accent, 0.95f, 0.85f);
    c[ImGuiCol_CheckMark] = g_accent;
    c[ImGuiCol_SliderGrab] = shade(g_accent, 0.85f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = g_accent;
    c[ImGuiCol_Separator] = shade(g_accent, 0.60f, 0.45f);
    c[ImGuiCol_SeparatorHovered] = shade(g_accent, 0.85f, 0.70f);
    c[ImGuiCol_TitleBgActive] = shade(g_accent, 0.35f, 1.00f);
    c[ImGuiCol_PlotHistogram] = shade(g_accent, 0.90f, 1.00f);
    c[ImGuiCol_TextSelectedBg] = shade(g_accent, 0.80f, 0.35f);

    // One consistent DPI scale for fonts and all size metrics.
    style.FontScaleDpi = g_fontScale;
    if (g_fontScale != 1.0f)
        style.ScaleAllSizes(g_fontScale);

    ImGui::GetStyle() = style;
}

} // namespace

void dark()
{
    g_dark = true;
    apply();
}

void light()
{
    g_dark = false;
    apply();
}

void setAccent(const ImVec4& color)
{
    g_accent = color;
    apply();
}

ImVec4 accent()
{
    return g_accent;
}

void setFontScale(float scale)
{
    g_fontScale = scale > 0.0f ? scale : 1.0f;
    apply();
}

float fontScale()
{
    return g_fontScale;
}

void rounding(float radius)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = radius;
    style.ChildRounding = radius;
    style.FrameRounding = radius;
    style.PopupRounding = radius;
    style.GrabRounding = radius;
}

} // namespace theme
} // namespace echo
