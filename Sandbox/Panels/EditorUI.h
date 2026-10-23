#pragma once
// Small widget kit shared by the editor panels: vector icons drawn with ImDrawList
// (the editor ships no icon font), icon/toggle buttons and section helpers, all
// styled from the theme set up in ImGuiLayer.
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <cmath>
#include <algorithm>

namespace EditorUI{

    // The theme's accent and a few semantic colours (kept in sync with ImGuiLayer).
    inline ImU32 Accent(float alpha = 1.0f)   { return ImGui::GetColorU32(ImVec4(0.941f, 0.522f, 0.227f, alpha)); }
    inline ImU32 Success(float alpha = 1.0f)  { return ImGui::GetColorU32(ImVec4(0.36f, 0.78f, 0.46f, alpha)); }
    inline ImU32 Danger(float alpha = 1.0f)   { return ImGui::GetColorU32(ImVec4(0.89f, 0.30f, 0.30f, alpha)); }
    inline ImU32 TextColor(float alpha = 1.0f){ ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text); c.w *= alpha; return ImGui::GetColorU32(c); }
    inline ImU32 MutedColor()                 { return ImGui::GetColorU32(ImGuiCol_TextDisabled); }

    enum class Icon{
        Logo, Play, Stop, Pause,
        Minimize, Maximize, Restore, Close,
        Select, Move, Rotate, Scale,
        Plus, Search, More, Local, World, Magnet,
    };

    // Draw icon centred on c, fitting a size x size box.
    inline void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col){
        const float h = size * 0.5f;
        const float t = std::max(1.0f, size / 11.0f); // stroke thickness
        switch (icon){
            case Icon::Logo:{
                // A stylised flame: two stacked teardrops in the accent colours.
                auto flame = [&](ImVec2 base, float w, float ht, ImU32 fill){
                    dl->PathLineTo(ImVec2(base.x, base.y - ht));
                    dl->PathBezierCubicCurveTo(ImVec2(base.x + w * 0.2f, base.y - ht * 0.55f),
                        ImVec2(base.x + w, base.y - ht * 0.45f), ImVec2(base.x + w * 0.55f, base.y));
                    dl->PathLineTo(ImVec2(base.x - w * 0.55f, base.y));
                    dl->PathBezierCubicCurveTo(ImVec2(base.x - w, base.y - ht * 0.45f),
                        ImVec2(base.x - w * 0.2f, base.y - ht * 0.55f), ImVec2(base.x, base.y - ht));
                    dl->PathFillConvex(fill);
                };
                ImVec2 base(c.x, c.y + h * 0.85f);
                flame(base, h * 1.15f, size * 0.95f, IM_COL32(232, 84, 46, 255));
                flame(ImVec2(base.x, base.y - h * 0.05f), h * 0.7f, size * 0.62f, IM_COL32(255, 176, 64, 255));
                dl->AddCircleFilled(ImVec2(base.x, base.y - h * 0.35f), h * 0.22f, IM_COL32(255, 236, 170, 255));
                break;
            }
            case Icon::Play:
                dl->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.75f), ImVec2(c.x + h * 0.8f, c.y),
                                      ImVec2(c.x - h * 0.6f, c.y + h * 0.75f), col);
                break;
            case Icon::Stop:
                dl->AddRectFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.6f), ImVec2(c.x + h * 0.6f, c.y + h * 0.6f), col, 1.5f);
                break;
            case Icon::Pause:
                dl->AddRectFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.7f), ImVec2(c.x - h * 0.15f, c.y + h * 0.7f), col, 1.0f);
                dl->AddRectFilled(ImVec2(c.x + h * 0.15f, c.y - h * 0.7f), ImVec2(c.x + h * 0.6f, c.y + h * 0.7f), col, 1.0f);
                break;
            case Icon::Minimize:
                dl->AddLine(ImVec2(c.x - h * 0.5f, c.y), ImVec2(c.x + h * 0.5f, c.y), col, 1.0f);
                break;
            case Icon::Maximize:
                dl->AddRect(ImVec2(c.x - h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), col, 0.0f, 0, 1.0f);
                break;
            case Icon::Restore:
                dl->AddRect(ImVec2(c.x - h * 0.5f, c.y - h * 0.3f), ImVec2(c.x + h * 0.3f, c.y + h * 0.5f), col, 0.0f, 0, 1.0f);
                dl->AddLine(ImVec2(c.x - h * 0.3f, c.y - h * 0.3f), ImVec2(c.x - h * 0.3f, c.y - h * 0.5f), col, 1.0f);
                dl->AddLine(ImVec2(c.x - h * 0.3f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y - h * 0.5f), col, 1.0f);
                dl->AddLine(ImVec2(c.x + h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.3f), col, 1.0f);
                dl->AddLine(ImVec2(c.x + h * 0.5f, c.y + h * 0.3f), ImVec2(c.x + h * 0.3f, c.y + h * 0.3f), col, 1.0f);
                break;
            case Icon::Close:
                dl->AddLine(ImVec2(c.x - h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), col, 1.2f);
                dl->AddLine(ImVec2(c.x + h * 0.5f, c.y - h * 0.5f), ImVec2(c.x - h * 0.5f, c.y + h * 0.5f), col, 1.2f);
                break;
            case Icon::Select:{
                // Mouse pointer.
                ImVec2 p[] = {
                    ImVec2(c.x - h * 0.55f, c.y - h * 0.8f), ImVec2(c.x + h * 0.55f, c.y + h * 0.25f),
                    ImVec2(c.x + h * 0.0f,  c.y + h * 0.3f), ImVec2(c.x - h * 0.2f,  c.y + h * 0.85f),
                    ImVec2(c.x - h * 0.4f,  c.y + h * 0.75f), ImVec2(c.x - h * 0.2f, c.y + h * 0.25f),
                    ImVec2(c.x - h * 0.55f, c.y + h * 0.45f),
                };
                dl->AddPolyline(p, 7, col, ImDrawFlags_Closed, t);
                break;
            }
            case Icon::Move:{
                const float a = h * 0.85f, w = h * 0.28f;
                dl->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x + a, c.y), col, t);
                dl->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y + a), col, t);
                dl->AddTriangleFilled(ImVec2(c.x + a + 1, c.y), ImVec2(c.x + a - w * 1.4f, c.y - w), ImVec2(c.x + a - w * 1.4f, c.y + w), col);
                dl->AddTriangleFilled(ImVec2(c.x - a - 1, c.y), ImVec2(c.x - a + w * 1.4f, c.y + w), ImVec2(c.x - a + w * 1.4f, c.y - w), col);
                dl->AddTriangleFilled(ImVec2(c.x, c.y - a - 1), ImVec2(c.x + w, c.y - a + w * 1.4f), ImVec2(c.x - w, c.y - a + w * 1.4f), col);
                dl->AddTriangleFilled(ImVec2(c.x, c.y + a + 1), ImVec2(c.x - w, c.y + a - w * 1.4f), ImVec2(c.x + w, c.y + a - w * 1.4f), col);
                break;
            }
            case Icon::Rotate:{
                const float r = h * 0.7f;
                dl->PathArcTo(c, r, IM_PI * 0.15f, IM_PI * 1.75f, 20);
                dl->PathStroke(col, 0, t);
                ImVec2 tip(c.x + r * cosf(IM_PI * 0.15f), c.y + r * sinf(IM_PI * 0.15f));
                dl->AddTriangleFilled(ImVec2(tip.x - h * 0.35f, tip.y - h * 0.05f), ImVec2(tip.x + h * 0.32f, tip.y - h * 0.1f),
                                      ImVec2(tip.x, tip.y + h * 0.4f), col);
                break;
            }
            case Icon::Scale:{
                dl->AddRect(ImVec2(c.x - h * 0.75f, c.y - h * 0.1f), ImVec2(c.x + h * 0.1f, c.y + h * 0.75f), col, 1.0f, 0, t);
                dl->AddLine(ImVec2(c.x - h * 0.1f, c.y + h * 0.1f), ImVec2(c.x + h * 0.7f, c.y - h * 0.7f), col, t);
                dl->AddLine(ImVec2(c.x + h * 0.15f, c.y - h * 0.75f), ImVec2(c.x + h * 0.75f, c.y - h * 0.75f), col, t);
                dl->AddLine(ImVec2(c.x + h * 0.75f, c.y - h * 0.75f), ImVec2(c.x + h * 0.75f, c.y - h * 0.15f), col, t);
                break;
            }
            case Icon::Plus:
                dl->AddLine(ImVec2(c.x - h * 0.6f, c.y), ImVec2(c.x + h * 0.6f, c.y), col, t * 1.2f);
                dl->AddLine(ImVec2(c.x, c.y - h * 0.6f), ImVec2(c.x, c.y + h * 0.6f), col, t * 1.2f);
                break;
            case Icon::Search:
                dl->AddCircle(ImVec2(c.x - h * 0.15f, c.y - h * 0.15f), h * 0.5f, col, 16, t);
                dl->AddLine(ImVec2(c.x + h * 0.22f, c.y + h * 0.22f), ImVec2(c.x + h * 0.7f, c.y + h * 0.7f), col, t * 1.3f);
                break;
            case Icon::More:
                for (int i = -1; i <= 1; i++)
                    dl->AddCircleFilled(ImVec2(c.x, c.y + i * h * 0.55f), std::max(1.2f, h * 0.14f), col);
                break;
            case Icon::Local:{
                // Isometric cube.
                const float r = h * 0.75f;
                ImVec2 top(c.x, c.y - r), l(c.x - r * 0.87f, c.y - r * 0.5f), rr(c.x + r * 0.87f, c.y - r * 0.5f);
                ImVec2 mid(c.x, c.y), bl(c.x - r * 0.87f, c.y + r * 0.5f), br(c.x + r * 0.87f, c.y + r * 0.5f), bot(c.x, c.y + r);
                ImVec2 outline[] = { top, rr, br, bot, bl, l };
                dl->AddPolyline(outline, 6, col, ImDrawFlags_Closed, t);
                dl->AddLine(l, mid, col, t); dl->AddLine(rr, mid, col, t); dl->AddLine(mid, bot, col, t);
                break;
            }
            case Icon::World:
                dl->AddCircle(c, h * 0.75f, col, 20, t);
                for (int i = 0; i < 18; i++){
                    float a = IM_PI * 2.0f * i / 18.0f;
                    dl->PathLineTo(ImVec2(c.x + cosf(a) * h * 0.32f, c.y + sinf(a) * h * 0.75f));
                }
                dl->PathStroke(col, ImDrawFlags_Closed, t);
                dl->AddLine(ImVec2(c.x - h * 0.75f, c.y), ImVec2(c.x + h * 0.75f, c.y), col, t);
                break;
            case Icon::Magnet:{
                const float r = h * 0.45f;
                dl->PathArcTo(ImVec2(c.x, c.y + h * 0.05f), r, 0.0f, IM_PI, 14);
                dl->PathStroke(col, 0, h * 0.32f);
                dl->AddRectFilled(ImVec2(c.x - r - h * 0.16f, c.y - h * 0.7f), ImVec2(c.x - r + h * 0.16f, c.y + h * 0.05f), col);
                dl->AddRectFilled(ImVec2(c.x + r - h * 0.16f, c.y - h * 0.7f), ImVec2(c.x + r + h * 0.16f, c.y + h * 0.05f), col);
                break;
            }
        }
    }

    // A flat button showing an icon; `active` keeps it highlighted (toggle/tool state).
    inline bool IconButton(const char* id, Icon icon, ImVec2 size, bool active = false,
                           const char* tooltip = nullptr, float iconScale = 0.5f){
        bool clicked = ImGui::InvisibleButton(id, size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        float rounding = ImGui::GetStyle().FrameRounding;
        if (active)
            dl->AddRectFilled(mn, mx, Accent(held ? 0.45f : 0.30f), rounding);
        else if (hovered || held)
            dl->AddRectFilled(mn, mx, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), rounding);
        ImU32 col = active ? Accent() : (hovered ? TextColor() : TextColor(0.78f));
        DrawIcon(dl, icon, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), std::min(size.x, size.y) * iconScale, col);
        if (tooltip && hovered)
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    // Muted, upper-case-style label for grouping controls inside a panel.
    inline void SectionLabel(const char* text){
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - 2.0f),
            ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - 2.0f), ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Spacing();
    }

    // Centred, muted hint for panels that have nothing to show yet.
    inline void EmptyState(const char* title, const char* hint = nullptr){
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        float blockH = hint ? lineH * 2.0f : lineH;
        float y = ImGui::GetCursorPosY() + std::max(0.0f, (std::min(avail.y, 220.0f) - blockH) * 0.5f);
        auto centred = [&](const char* s, bool muted){
            float w = ImGui::CalcTextSize(s).x;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - w) * 0.5f), y));
            if (muted) { ImGui::TextDisabled("%s", s); } else { ImGui::TextUnformatted(s); }
            y += lineH;
        };
        float x0 = ImGui::GetCursorPosX();
        centred(title, false);
        if (hint){ ImGui::SetCursorPosX(x0); centred(hint, true); }
    }

    // Rounded "pill" with a coloured dot (status indicators). PillSize/DrawPill only
    // draw, so they can sit on top of other content (the viewport image).
    inline ImVec2 PillSize(const char* text, float padY = 3.0f){
        ImVec2 ts = ImGui::CalcTextSize(text);
        return ImVec2(8.0f * 2.0f + 6.0f + 6.0f + ts.x, ts.y + padY * 2.0f);
    }
    inline void DrawPill(ImDrawList* dl, ImVec2 p, const char* text, ImU32 dotColor, ImU32 bg, float padY = 3.0f){
        ImVec2 size = PillSize(text, padY);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, size.y * 0.5f);
        dl->AddCircleFilled(ImVec2(p.x + 8.0f + 3.0f, p.y + size.y * 0.5f), 3.0f, dotColor);
        dl->AddText(ImVec2(p.x + 8.0f + 6.0f + 6.0f, p.y + padY), TextColor(), text);
    }
}
