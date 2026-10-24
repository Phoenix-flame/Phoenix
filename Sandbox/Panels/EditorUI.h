#pragma once
// Small widget kit shared by the editor panels: vector icons drawn with ImDrawList
// (the editor ships no icon font), icon/toggle buttons and section helpers, all
// styled from the theme set up in ImGuiLayer.
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

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
        Eye, StepForward, Reset, Bolt,
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
            case Icon::Eye:{
                for (int i = 0; i <= 16; i++){
                    float a = IM_PI * (float)i / 16.0f;
                    dl->PathLineTo(ImVec2(c.x - cosf(a) * h * 0.85f, c.y - sinf(a) * h * 0.5f));
                }
                for (int i = 1; i < 16; i++){
                    float a = IM_PI * (float)i / 16.0f;
                    dl->PathLineTo(ImVec2(c.x + cosf(a) * h * 0.85f, c.y + sinf(a) * h * 0.5f));
                }
                dl->PathStroke(col, ImDrawFlags_Closed, t);
                dl->AddCircleFilled(c, h * 0.24f, col);
                break;
            }
            case Icon::StepForward:
                dl->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.65f), ImVec2(c.x + h * 0.35f, c.y),
                                      ImVec2(c.x - h * 0.6f, c.y + h * 0.65f), col);
                dl->AddRectFilled(ImVec2(c.x + h * 0.4f, c.y - h * 0.65f), ImVec2(c.x + h * 0.65f, c.y + h * 0.65f), col);
                break;
            case Icon::Reset:{
                const float r = h * 0.65f;
                dl->PathArcTo(c, r, IM_PI * 0.6f, IM_PI * 2.2f, 20);
                dl->PathStroke(col, 0, t);
                ImVec2 tip(c.x + r * cosf(IM_PI * 0.6f), c.y + r * sinf(IM_PI * 0.6f));
                dl->AddTriangleFilled(ImVec2(tip.x - h * 0.4f, tip.y), ImVec2(tip.x + h * 0.25f, tip.y - h * 0.3f),
                                      ImVec2(tip.x + h * 0.2f, tip.y + h * 0.35f), col);
                break;
            }
            case Icon::Bolt:{
                ImVec2 p[] = { ImVec2(c.x + h * 0.2f, c.y - h * 0.85f), ImVec2(c.x - h * 0.5f, c.y + h * 0.1f),
                               ImVec2(c.x - h * 0.02f, c.y + h * 0.1f), ImVec2(c.x - h * 0.2f, c.y + h * 0.85f),
                               ImVec2(c.x + h * 0.5f, c.y - h * 0.15f), ImVec2(c.x + h * 0.02f, c.y - h * 0.15f) };
                dl->AddConvexPolyFilled(p, 3, col);
                ImVec2 q[] = { p[2], p[3], p[4], p[5] };
                dl->AddConvexPolyFilled(q, 4, col);
                ImVec2 r[] = { p[0], p[1], p[2], p[5] };
                dl->AddConvexPolyFilled(r, 4, col);
                break;
            }
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

    // Button with an icon in front of its label. `primary` fills it with the accent.
    inline bool IconTextButton(const char* id, Icon icon, const char* label, bool primary = false,
                               ImU32 iconColor = 0, float width = 0.0f){
        ImVec2 ts = ImGui::CalcTextSize(label);
        float h = ImGui::GetFrameHeight();
        ImVec2 size(width > 0.0f ? width : ts.x + h + 14.0f, h);
        bool clicked = ImGui::InvisibleButton(id, size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        float rounding = ImGui::GetStyle().FrameRounding;
        ImU32 bg = primary ? Accent(held ? 0.7f : hovered ? 1.0f : 0.88f)
                           : ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        dl->AddRectFilled(mn, mx, bg, rounding);
        ImU32 text = primary ? IM_COL32(20, 20, 22, 255) : TextColor();
        float contentW = h * 0.55f + 6.0f + ts.x;
        float x = mn.x + (size.x - contentW) * 0.5f;
        DrawIcon(dl, icon, ImVec2(x + h * 0.275f, (mn.y + mx.y) * 0.5f), h * 0.5f, iconColor ? iconColor : text);
        dl->AddText(ImVec2(x + h * 0.55f + 6.0f, mn.y + (h - ts.y) * 0.5f), text, label);
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

    // Row of mutually exclusive buttons (a "segmented control"). Returns true on change.
    inline bool Segmented(const char* id, int* value, const char* const* labels, int count,
                          float width = 0.0f, const char* const* tooltips = nullptr){
        ImGui::PushID(id);
        const ImGuiStyle& style = ImGui::GetStyle();
        float total = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + total, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
        bool changed = false;
        float segment = total / (float)count;
        for (int i = 0; i < count; i++){
            ImGui::SetCursorScreenPos(ImVec2(p.x + segment * i, p.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##seg", ImVec2(segment, h)) && *value != i){ *value = i; changed = true; }
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImVec2 mn(p.x + segment * i + 2.0f, p.y + 2.0f), mx(p.x + segment * (i + 1) - 2.0f, p.y + h - 2.0f);
            if (*value == i)  { dl->AddRectFilled(mn, mx, Accent(0.85f), style.FrameRounding - 1.0f); }
            else if (hovered) { dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), style.FrameRounding - 1.0f); }
            ImVec2 ts = ImGui::CalcTextSize(labels[i]);
            ImU32 col = *value == i ? IM_COL32(20, 20, 22, 255) : TextColor(hovered ? 1.0f : 0.8f);
            dl->PushClipRect(mn, mx, true);
            dl->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), col, labels[i]);
            dl->PopClipRect();
            if (hovered && tooltips && tooltips[i]) { ImGui::SetTooltip("%s", tooltips[i]); }
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(total, h));
        ImGui::PopID();
        return changed;
    }

    // Card with a muted caption, a large value and an optional detail line.
    inline void StatTile(const char* label, const char* value, const char* detail, float width, ImU32 accent = 0){
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* big = ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont();
        const float h = 58.0f;
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
        if (accent) { dl->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + h), accent, 6.0f, ImDrawFlags_RoundCornersLeft); }
        dl->PushClipRect(p, ImVec2(p.x + width, p.y + h), true);
        dl->AddText(ImVec2(p.x + 10.0f, p.y + 6.0f), MutedColor(), label);
        dl->AddText(big, big->FontSize, ImVec2(p.x + 10.0f, p.y + 22.0f), TextColor(), value);
        if (detail){
            ImFont* small = ImGui::GetIO().Fonts->Fonts.Size > 3 ? ImGui::GetIO().Fonts->Fonts[3] : ImGui::GetFont();
            float tw = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, detail).x;
            dl->AddText(small, small->FontSize, ImVec2(p.x + width - tw - 8.0f, p.y + 8.0f), MutedColor(), detail);
        }
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(width, h));
    }

    // Line chart of up to two series sharing the x axis (e.g. raw values + moving
    // average), with min/max gridlines and a hover readout.
    inline void LineChart(const char* id, const std::vector<float>& a, const std::vector<float>* b, ImVec2 size,
                          ImU32 colorA, ImU32 colorB, const char* format = "%.2f", const char* xLabel = nullptr){
        ImGui::PushID(id);
        if (size.x <= 0.0f) { size.x = ImGui::GetContentRegionAvail().x; }
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##chart", size);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 q(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);

        const size_t n = a.size();
        if (n < 2){
            const char* msg = "Waiting for data";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), MutedColor(), msg);
            ImGui::PopID();
            return;
        }
        float lo = *std::min_element(a.begin(), a.end()), hi = *std::max_element(a.begin(), a.end());
        if (b && !b->empty()){
            lo = std::min(lo, *std::min_element(b->begin(), b->end()));
            hi = std::max(hi, *std::max_element(b->begin(), b->end()));
        }
        if (hi - lo < 1e-4f) { hi += 0.5f; lo -= 0.5f; }
        float pad = (hi - lo) * 0.08f;
        hi += pad; lo -= pad;

        const float left = p.x + 44.0f, right = q.x - 8.0f, top = p.y + 8.0f, bottom = q.y - (xLabel ? 18.0f : 8.0f);
        auto X = [&](size_t i){ return left + (right - left) * (float)i / (float)(n - 1); };
        auto Y = [&](float v){ return bottom - (bottom - top) * (v - lo) / (hi - lo); };

        // Grid: max, zero (if visible), min.
        char buf[32];
        auto gridLine = [&](float v, bool strong){
            float y = Y(v);
            dl->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(255, 255, 255, strong ? 40 : 18));
            std::snprintf(buf, sizeof(buf), format, v);
            dl->AddText(ImVec2(p.x + 6.0f, y - ImGui::GetFontSize() * 0.5f), MutedColor(), buf);
        };
        gridLine(hi - pad, false);
        if (Y(lo + pad) - Y(hi - pad) > ImGui::GetFontSize()) { gridLine(lo + pad, false); }
        if (lo < 0.0f && hi > 0.0f && std::min(Y(0.0f) - Y(hi - pad), Y(lo + pad) - Y(0.0f)) > ImGui::GetFontSize())
            gridLine(0.0f, true);
        if (xLabel){
            ImVec2 ts = ImGui::CalcTextSize(xLabel);
            dl->AddText(ImVec2(right - ts.x, bottom + 3.0f), MutedColor(), xLabel);
        }

        dl->PushClipRect(ImVec2(left, top - 2.0f), ImVec2(right + 2.0f, bottom + 2.0f), true);
        // Decimate to about one point per pixel.
        auto drawSeries = [&](const std::vector<float>& s, ImU32 col, float thickness){
            size_t count = std::min(s.size(), n);
            size_t stride = std::max<size_t>(1, count / (size_t)std::max(1.0f, right - left));
            for (size_t i = 0; i < count; i += stride) { dl->PathLineTo(ImVec2(X(i), Y(s[i]))); }
            dl->PathLineTo(ImVec2(X(count - 1), Y(s[count - 1])));
            dl->PathStroke(col, 0, thickness);
        };
        drawSeries(a, colorA, 1.0f);
        if (b && b->size() >= 2) { drawSeries(*b, colorB, 2.0f); }
        dl->PopClipRect();

        if (hovered){
            float mx = ImGui::GetIO().MousePos.x;
            size_t i = (size_t)std::clamp((int)std::lround((mx - left) / (right - left) * (float)(n - 1)), 0, (int)n - 1);
            dl->AddLine(ImVec2(X(i), top), ImVec2(X(i), bottom), IM_COL32(255, 255, 255, 60));
            dl->AddCircleFilled(ImVec2(X(i), Y(a[i])), 3.0f, colorA);
            if (b && i < b->size()){
                dl->AddCircleFilled(ImVec2(X(i), Y((*b)[i])), 3.5f, colorB);
                ImGui::SetTooltip("#%d   value %.3f   average %.3f", (int)i + 1, a[i], (*b)[i]);
            }
            else { ImGui::SetTooltip("#%d   %.3f", (int)i + 1, a[i]); }
        }
        ImGui::PopID();
    }

    // Horizontal bar for a value in [min, max] with the zero line marked (signed data).
    inline void ValueBar(float value, float min, float max, ImVec2 size, ImU32 color){
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (size.x <= 0.0f) { size.x = ImGui::GetContentRegionAvail().x; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        auto X = [&](float v){ return p.x + size.x * (std::clamp(v, min, max) - min) / (max - min); };
        float zero = X(std::clamp(0.0f, min, max));
        float x = X(value);
        dl->AddRectFilled(ImVec2(std::min(zero, x), p.y + 2.0f), ImVec2(std::max(zero, x) + 1.0f, p.y + size.y - 2.0f), color, 2.0f);
        if (min < 0.0f) { dl->AddLine(ImVec2(zero, p.y), ImVec2(zero, p.y + size.y), IM_COL32(255, 255, 255, 50)); }
        ImGui::Dummy(size);
    }

    // Trailing moving average (window w) of a series.
    inline std::vector<float> MovingAverage(const std::vector<float>& v, int window){
        std::vector<float> out(v.size());
        double sum = 0.0;
        for (size_t i = 0; i < v.size(); i++){
            sum += v[i];
            if (i >= (size_t)window) { sum -= v[i - window]; }
            out[i] = (float)(sum / (double)std::min(i + 1, (size_t)window));
        }
        return out;
    }

    // Distinct colours for categories (ray sensor tags, chart series).
    inline ImU32 CategoryColor(int i){
        static const ImU32 colors[] = {
            IM_COL32(80, 220, 120, 255), IM_COL32(150, 160, 185, 255), IM_COL32(240, 80, 80, 255),
            IM_COL32(90, 170, 255, 255), IM_COL32(240, 200, 70, 255), IM_COL32(200, 120, 255, 255),
        };
        return colors[((i % 6) + 6) % 6];
    }
}
