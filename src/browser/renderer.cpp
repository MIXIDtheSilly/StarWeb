#include "renderer.hpp"
#include "parser.hpp"
#include "fetcher.hpp"
#include "globals.hpp"
#include "theme.hpp"
#include "media_player.hpp"
#include "../common/url_parser.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <filesystem>

InputStyleGuard::InputStyleGuard(const CssStyle& merged) {
    float rounding = merged.border_radius >= 0.0f ? merged.border_radius : 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, merged.border_width >= 0.0f ? merged.border_width : 1.0f);
    
    float pad_x = merged.padding_left > 0.0f ? merged.padding_left : ImGui::GetStyle().FramePadding.x;
    float pad_y = merged.padding_top > 0.0f ? merged.padding_top : ImGui::GetStyle().FramePadding.y;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad_x, pad_y));
    
    ImVec4 frame_bg = merged.has_bg ? merged.bg_color : ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frame_bg);
    
    ImVec4 frame_bg_hovered = merged.has_bg 
        ? ImVec4(frame_bg.x * 0.95f, frame_bg.y * 0.95f, frame_bg.z * 0.95f, frame_bg.w) 
        : ImVec4(0.22f, 0.20f, 0.26f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frame_bg_hovered);
    
    ImVec4 frame_bg_active = merged.has_bg 
        ? ImVec4(frame_bg.x * 0.90f, frame_bg.y * 0.90f, frame_bg.z * 0.90f, frame_bg.w) 
        : ImVec4(0.28f, 0.24f, 0.35f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frame_bg_active);
    
    ImVec4 text_color = merged.has_color ? merged.color : ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    
    ImVec4 border_color = merged.has_border_color ? merged.border_color : ImVec4(0.24f, 0.20f, 0.35f, 0.60f);
    ImGui::PushStyleColor(ImGuiCol_Border, border_color);
    
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, text_color);
}

InputStyleGuard::~InputStyleGuard() {
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
}

bool is_inline_element(const DomNode& node, const CssStyle& merged) {
    if (merged.display == "inline" || merged.display == "inline-block") return true;
    if (merged.display == "block") return false;
    
    if (node.tag == "span" || node.tag == "a" || node.tag == "button" || 
        node.tag == "input" || node.tag == "select" || node.tag == "option") {
        return true;
    }
    return false;
}

std::string get_media_source(const DomNode& node) {
    if (!node.src.empty()) {
        return node.src;
    }
    for (const auto& child : node.children) {
        if (child.tag == "source" && !child.src.empty()) {
            return child.src;
        }
    }
    return "";
}

// Build a closed polygon path with rounded corners (Lucide-style rounded joins).
static void BuildRoundedPolyPath(ImDrawList* draw_list, const ImVec2* pts, int n, float r) {
    draw_list->PathClear();
    for (int i = 0; i < n; i++) {
        ImVec2 cur  = pts[i];
        ImVec2 prev = pts[(i + n - 1) % n];
        ImVec2 next = pts[(i + 1) % n];
        ImVec2 d1(prev.x - cur.x, prev.y - cur.y);
        ImVec2 d2(next.x - cur.x, next.y - cur.y);
        float l1 = sqrtf(d1.x * d1.x + d1.y * d1.y);
        float l2 = sqrtf(d2.x * d2.x + d2.y * d2.y);
        if (l1 > 0.0f) { d1.x /= l1; d1.y /= l1; }
        if (l2 > 0.0f) { d2.x /= l2; d2.y /= l2; }
        float rr = std::min(r, std::min(l1, l2) * 0.5f);
        ImVec2 p_in (cur.x + d1.x * rr, cur.y + d1.y * rr);
        ImVec2 p_out(cur.x + d2.x * rr, cur.y + d2.y * rr);
        draw_list->PathLineTo(p_in);
        draw_list->PathBezierQuadraticCurveTo(cur, p_out, 8);
    }
}

void DrawPlayIcon(ImDrawList* draw_list, ImVec2 center, ImU32 color, float size) {
    // Lucide "play": right-pointing triangle outline with rounded corners.
    ImVec2 pts[3] = {
        ImVec2(center.x - size * 0.45f, center.y - size * 0.62f),
        ImVec2(center.x - size * 0.45f, center.y + size * 0.62f),
        ImVec2(center.x + size * 0.72f, center.y),
    };
    BuildRoundedPolyPath(draw_list, pts, 3, size * 0.22f);
    draw_list->PathStroke(color, ImDrawFlags_Closed, std::max(1.4f, size * 0.16f));
}

void DrawPauseIcon(ImDrawList* draw_list, ImVec2 center, ImU32 color, float size) {
    // Lucide "pause": two rounded vertical bar outlines.
    float thickness = std::max(1.4f, size * 0.16f);
    float bar_w = size * 0.42f;
    float bar_h = size * 1.40f;
    float gap   = bar_w * 0.40f;
    float r     = bar_w * 0.24f;
    draw_list->AddRect(ImVec2(center.x - gap - bar_w, center.y - bar_h * 0.5f),
                       ImVec2(center.x - gap,         center.y + bar_h * 0.5f), color, r, 0, thickness);
    draw_list->AddRect(ImVec2(center.x + gap,         center.y - bar_h * 0.5f),
                       ImVec2(center.x + gap + bar_w, center.y + bar_h * 0.5f), color, r, 0, thickness);
}

void DrawSpeakerIcon(ImDrawList* draw_list, ImVec2 center, ImU32 color, float size, bool is_muted) {
    // Lucide "volume-2": rounded speaker outline plus two concentric sound-wave arcs.
    float thickness = std::max(1.4f, size * 0.15f);
    float box_x  = center.x - size * 0.95f;   // left edge of the base box
    float neck_x = center.x - size * 0.45f;   // where box meets the cone
    float cone_x = center.x + size * 0.05f;   // cone's wide (right) opening
    float box_h  = size * 0.40f;              // half-height of neck/box
    float cone_h = size * 0.85f;              // half-height of cone opening

    // Speaker outline (single closed polygon with rounded corners: box on the left, cone on the right).
    ImVec2 pts[6] = {
        ImVec2(cone_x, center.y - cone_h),
        ImVec2(neck_x, center.y - box_h),
        ImVec2(box_x,  center.y - box_h),
        ImVec2(box_x,  center.y + box_h),
        ImVec2(neck_x, center.y + box_h),
        ImVec2(cone_x, center.y + cone_h),
    };
    BuildRoundedPolyPath(draw_list, pts, 6, size * 0.16f);
    draw_list->PathStroke(color, ImDrawFlags_Closed, thickness);

    if (is_muted) {
        float x0 = center.x + size * 0.60f;
        float xs = size * 0.40f;
        draw_list->AddLine(ImVec2(x0 - xs, center.y - xs), ImVec2(x0 + xs, center.y + xs), color, thickness);
        draw_list->AddLine(ImVec2(x0 - xs, center.y + xs), ImVec2(x0 + xs, center.y - xs), color, thickness);
    } else {
        ImVec2 arc_c(center.x + size * 0.05f, center.y);
        draw_list->PathClear();
        draw_list->PathArcTo(arc_c, size * 0.50f, -0.60f, 0.60f, 10);
        draw_list->PathStroke(color, 0, thickness);
        draw_list->PathClear();
        draw_list->PathArcTo(arc_c, size * 0.85f, -0.60f, 0.60f, 12);
        draw_list->PathStroke(color, 0, thickness);
    }
}

void DrawFullscreenIcon(ImDrawList* draw_list, ImVec2 center, ImU32 color, float size) {
    float r = size * 0.45f;
    float gap = size * 0.25f;
    float thickness = 1.5f;
    // Top-left
    draw_list->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x - r + gap, center.y - r), color, thickness);
    draw_list->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x - r, center.y - r + gap), color, thickness);
    // Top-right
    draw_list->AddLine(ImVec2(center.x + r, center.y - r), ImVec2(center.x + r - gap, center.y - r), color, thickness);
    draw_list->AddLine(ImVec2(center.x + r, center.y - r), ImVec2(center.x + r, center.y - r + gap), color, thickness);
    // Bottom-left
    draw_list->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x - r + gap, center.y + r), color, thickness);
    draw_list->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x - r, center.y + r - gap), color, thickness);
    // Bottom-right
    draw_list->AddLine(ImVec2(center.x + r, center.y + r), ImVec2(center.x + r - gap, center.y + r), color, thickness);
    draw_list->AddLine(ImVec2(center.x + r, center.y + r), ImVec2(center.x + r, center.y + r - gap), color, thickness);
}

// Shared media-control styling.
static const ImU32 kMediaIconColor   = IM_COL32(240, 240, 245, 255);
static const ImU32 kMediaTrackColor  = IM_COL32(120, 120, 120, 200);
static const ImU32 kMediaFillColor   = IM_COL32(255, 255, 255, 255);

// Vertical volume popup shown above the speaker button; handles drag and click-away close.
static void DrawVolumePopup(ImDrawList* draw_list, VideoPlayer* player, ImVec2 btn_min, ImVec2 btn_max,
                            ImGuiID open_id, const std::string& id_suffix) {
    float popup_w = 24.0f, popup_h = 80.0f, gap = 4.0f;
    ImVec2 popup_pos(btn_min.x + (btn_max.x - btn_min.x) * 0.5f - popup_w * 0.5f, btn_min.y - popup_h - gap);
    ImVec2 popup_max(popup_pos.x + popup_w, popup_pos.y + popup_h);

    draw_list->AddRectFilled(popup_pos, popup_max, IM_COL32(30, 30, 32, 255), 12.0f);
    draw_list->AddRect(popup_pos, popup_max, IM_COL32(255, 255, 255, 20), 12.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(popup_pos);
    ImGui::InvisibleButton(("##vol_slider_" + id_suffix).c_str(), ImVec2(popup_w, popup_h));
    if (ImGui::IsItemActive()) {
        float pct = ((popup_pos.y + popup_h - 10.0f) - ImGui::GetIO().MousePos.y) / (popup_h - 20.0f);
        pct = std::clamp(pct, 0.0f, 1.0f);
        player->set_volume(pct);
        if (pct > 0.0f && player->is_muted()) player->set_muted(false);
    }

    float vol = player->is_muted() ? 0.0f : player->get_volume();
    float track_x = popup_pos.x + popup_w * 0.5f;
    float track_top = popup_pos.y + 10.0f;
    float track_bottom = popup_pos.y + popup_h - 10.0f;
    float split_y = track_bottom - vol * (track_bottom - track_top);
    draw_list->AddRectFilled(ImVec2(track_x - 1.5f, track_top), ImVec2(track_x + 1.5f, track_bottom), kMediaTrackColor, 2.0f);
    if (vol > 0.0f) draw_list->AddRectFilled(ImVec2(track_x - 1.5f, split_y), ImVec2(track_x + 1.5f, track_bottom), kMediaFillColor, 2.0f);
    draw_list->AddCircleFilled(ImVec2(track_x, split_y), 5.0f, kMediaFillColor);

    // Click outside the popup or its button closes it.
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 m = ImGui::GetIO().MousePos;
        bool in_popup = m.x >= popup_pos.x && m.x <= popup_max.x && m.y >= popup_pos.y && m.y <= popup_max.y;
        bool in_btn   = m.x >= btn_min.x && m.x <= btn_max.x && m.y >= btn_min.y && m.y <= btn_max.y;
        if (!in_popup && !in_btn) ImGui::GetStateStorage()->SetBool(open_id, false);
    }
}

// Draws a Chrome-style control bar (play, timeline, time, volume) inside [bar_min, bar_max].
// Play is anchored to the left and volume to the right with symmetric padding; the timeline
// fills the space between the play button and the time label.
static void DrawMediaControlBar(ImDrawList* draw_list, VideoPlayer* player,
                                ImVec2 bar_min, ImVec2 bar_max, const std::string& id_suffix,
                                float icon_size) {
    const float pad = 12.0f, gap = 10.0f, play_sz = 26.0f, vol_sz = 24.0f;
    float cy = (bar_min.y + bar_max.y) * 0.5f;

    bool playing = player->is_playing();
    double duration = player->get_duration();
    double current_time = player->get_current_time();

    // Time label geometry (computed up front so the timeline can fill the remaining width).
    int dur_m = (int)duration / 60, dur_s = (int)duration % 60;
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d", (int)current_time / 60, (int)current_time % 60, dur_m, dur_s);
    ImVec2 ts = ImGui::CalcTextSize(time_buf);

    float vol_x  = bar_max.x - pad - vol_sz;
    float time_x = vol_x - gap - ts.x;
    float tl_x   = bar_min.x + pad + play_sz + gap;
    float tl_w   = time_x - gap - tl_x;

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 25));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 45));

    // Play / pause (left).
    ImGui::SetCursorScreenPos(ImVec2(bar_min.x + pad, cy - play_sz * 0.5f));
    if (ImGui::Button(("##play_" + id_suffix).c_str(), ImVec2(play_sz, play_sz))) {
        if (playing) player->pause(); else player->play();
    }
    ImVec2 pmin = ImGui::GetItemRectMin(), pmax = ImGui::GetItemRectMax();
    ImVec2 play_c((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);
    if (playing) DrawPauseIcon(draw_list, play_c, kMediaIconColor, icon_size);
    else         DrawPlayIcon(draw_list, play_c, kMediaIconColor, icon_size);

    // Volume button (right).
    ImGuiID vol_open_id = ImGui::GetID((id_suffix + "_vol_open").c_str());
    bool vol_open = ImGui::GetStateStorage()->GetBool(vol_open_id, false);
    ImGui::SetCursorScreenPos(ImVec2(vol_x, cy - vol_sz * 0.5f));
    if (ImGui::Button(("##vol_" + id_suffix).c_str(), ImVec2(vol_sz, vol_sz))) {
        vol_open = !vol_open;
        ImGui::GetStateStorage()->SetBool(vol_open_id, vol_open);
    }
    ImVec2 vmin = ImGui::GetItemRectMin(), vmax = ImGui::GetItemRectMax();
    DrawSpeakerIcon(draw_list, ImVec2((vmin.x + vmax.x) * 0.5f, (vmin.y + vmax.y) * 0.5f),
                    kMediaIconColor, icon_size, player->is_muted());

    ImGui::PopStyleColor(3);

    // Timeline (between play button and time label).
    if (tl_w > 30.0f) {
        ImGui::SetCursorScreenPos(ImVec2(tl_x, cy - 12.0f));
        ImGui::InvisibleButton(("##slider_" + id_suffix).c_str(), ImVec2(tl_w, 24.0f));
        bool active = ImGui::IsItemActive();
        if (active && duration > 0.0) {
            float pct = std::clamp((ImGui::GetIO().MousePos.x - tl_x) / tl_w, 0.0f, 1.0f);
            current_time = pct * duration;
            player->seek(current_time);
        }
        float pct = duration > 0.0f ? (float)(current_time / duration) : 0.0f;
        float split_x = tl_x + pct * tl_w;
        draw_list->AddRectFilled(ImVec2(tl_x, cy - 1.5f), ImVec2(tl_x + tl_w, cy + 1.5f), kMediaTrackColor, 2.0f);
        if (pct > 0.0f) draw_list->AddRectFilled(ImVec2(tl_x, cy - 1.5f), ImVec2(split_x, cy + 1.5f), kMediaFillColor, 2.0f);
        draw_list->AddCircleFilled(ImVec2(split_x, cy), active ? 6.0f : 5.0f, kMediaFillColor);
    }

    // Time label (vertically centred, drawn last so it reflects any seek this frame).
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d", (int)current_time / 60, (int)current_time % 60, dur_m, dur_s);
    draw_list->AddText(ImVec2(time_x, cy - ts.y * 0.5f), kMediaIconColor, time_buf);

    if (vol_open) DrawVolumePopup(draw_list, player, vmin, vmax, vol_open_id, id_suffix);
}

void render_node(DomNode& node, const CssStyle& parent_style, bool& is_inline_flow, Tab& tab, int li_index, float parent_accumulated_right) {
    if (node.tag == "script" || node.tag == "style" || node.tag == "head" || node.tag == "title" || node.tag == "meta" || node.tag == "option") {
        return;
    }

    CssStyle merged;
    if (parent_style.has_color) {
        merged.color = parent_style.color;
        merged.has_color = true;
    }
    merged.font_size = parent_style.font_size;
    merged.text_align = parent_style.text_align;
    auto tag_it = tab.css_classes.find(node.tag);
    if (tag_it != tab.css_classes.end()) {
        apply_style(merged, tag_it->second);
    }
    if (!node.class_name.empty()) {
        auto class_it = tab.css_classes.find("." + node.class_name);
        if (class_it != tab.css_classes.end()) {
            apply_style(merged, class_it->second);
        }
    }
    if (node.has_inline_style) {
        apply_style(merged, node.parsed_inline_style);
    }

    bool is_inline = is_inline_element(node, merged);
    if (is_inline) {
        if (is_inline_flow) {
            ImGui::SameLine(0, 8.0f + merged.margin_left);
        }
        is_inline_flow = true;
    } else {
        is_inline_flow = false;
    }

    bool draw_bg = (merged.has_bg || merged.has_gradient || (merged.border_width > 0.0f)) &&
                   (node.tag != "input" && node.tag != "textarea" && node.tag != "select" && node.tag != "button" && node.tag != "a");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImDrawListSplitter splitter;
    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImVec2 content_start = start_pos;

    float base_font_scale = merged.font_size;
    if (node.tag == "h1") base_font_scale *= 1.8f;
    else if (node.tag == "h2") base_font_scale *= 1.4f;
    else if (node.tag == "h3") base_font_scale *= 1.2f;
    else if (node.tag == "h4") base_font_scale *= 1.1f;
    else if (node.tag == "h5") base_font_scale *= 1.0f;
    else if (node.tag == "h6") base_font_scale *= 0.9f;

    if (base_font_scale != 1.0f) {
        ImGui::SetWindowFontScale(base_font_scale);
    }

    if (draw_bg) {
        if (!is_inline_flow && merged.margin_top > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.margin_top);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + merged.margin_left);
        
        content_start = ImGui::GetCursorScreenPos();
        
        if (!is_inline_flow && merged.padding_top > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.padding_top);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + merged.padding_left);
        
        splitter.Split(draw_list, 2);
        splitter.SetCurrentChannel(draw_list, 1);
    } else {
        bool is_widget = (node.tag == "input" || node.tag == "textarea" || node.tag == "select" || node.tag == "button");
        if (!is_inline_flow && merged.margin_top > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.margin_top);
        if (merged.margin_left > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + merged.margin_left);
        if (!is_widget && !is_inline_flow && merged.padding_top > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.padding_top);
        if (!is_widget && merged.padding_left > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + merged.padding_left);
    }

    ImGui::BeginGroup();

    float child_accumulated_right = parent_accumulated_right + merged.margin_right + merged.padding_right;
    if (node.tag == "div") {
        bool child_inline_flow = false;
        for (auto& child : node.children) {
            render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
        }
    } else if (node.tag == "ol") {
        int index = 1;
        bool child_inline_flow = false;
        for (auto& child : node.children) {
            if (child.tag == "li") {
                render_node(child, merged, child_inline_flow, tab, index++, child_accumulated_right);
            } else {
                render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
            }
        }
    } else if (node.tag == "ul") {
        bool child_inline_flow = false;
        for (auto& child : node.children) {
            render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
        }
    } else if (node.tag == "li") {
        std::string cleaned_text = collapse_whitespace(node.text_content);
        if (li_index >= 0) {
            ImGui::TextColored(merged.color, "%d. %s", li_index, cleaned_text.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, merged.color);
            ImGui::BulletText("%s", cleaned_text.c_str());
            ImGui::PopStyleColor();
        }
    } else if (node.tag == "h1" || node.tag == "h2" || node.tag == "h3" || node.tag == "h4" || node.tag == "h5" || node.tag == "h6" || node.tag == "p" || node.tag == "span") {
        std::string cleaned_text = collapse_whitespace(node.text_content);
        if (!cleaned_text.empty()) {
            float right_offset = parent_accumulated_right + merged.margin_right + merged.padding_right;
            if (merged.text_align == "center") {
                float text_width = ImGui::CalcTextSize(cleaned_text.c_str()).x;
                float avail_width = merged.width > 0.0f ? merged.width : (ImGui::GetContentRegionAvail().x - right_offset);
                if (avail_width < 0.0f) avail_width = 0.0f;
                float offset = (avail_width - text_width) * 0.5f;
                if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            } else if (merged.text_align == "right") {
                float text_width = ImGui::CalcTextSize(cleaned_text.c_str()).x;
                float avail_width = merged.width > 0.0f ? merged.width : (ImGui::GetContentRegionAvail().x - right_offset);
                if (avail_width < 0.0f) avail_width = 0.0f;
                float offset = avail_width - text_width;
                if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            }
            
            float wrap_width = merged.width > 0.0f ? merged.width : (ImGui::GetContentRegionAvail().x - right_offset);
            if (wrap_width < 0.0f) wrap_width = 0.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
            
            if (node.tag == "span") {
                ImGui::TextColored(merged.color, "%s", cleaned_text.c_str());
            } else {
                ImGui::TextColored(merged.color, "%s", cleaned_text.c_str());
                ImGui::Spacing();
            }
            
            ImGui::PopTextWrapPos();
        }
        
        bool child_inline_flow = (node.tag == "span") ? true : !cleaned_text.empty();
        float child_accumulated_right = parent_accumulated_right + merged.margin_right + merged.padding_right;
        for (auto& child : node.children) {
            render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
        }
    } else if (node.tag == "pre") {
        if (!node.text_content.empty()) {
            float right_offset = parent_accumulated_right + merged.margin_right + merged.padding_right;
            float wrap_width = merged.width > 0.0f ? merged.width : (ImGui::GetContentRegionAvail().x - right_offset);
            if (wrap_width < 0.0f) wrap_width = 0.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
            
            bool pushed_font = false;
            if (mono_font != nullptr) {
                ImGui::PushFont(mono_font);
                pushed_font = true;
            }
            
            ImGui::TextColored(merged.color, "%s", node.text_content.c_str());
            ImGui::Spacing();
            
            if (pushed_font) {
                ImGui::PopFont();
            }
            ImGui::PopTextWrapPos();
        }
        
        bool child_inline_flow = !node.text_content.empty();
        float child_accumulated_right = parent_accumulated_right + merged.margin_right + merged.padding_right;
        for (auto& child : node.children) {
            render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
        }
    } else if (node.tag == "button") {
        std::string cleaned_text = collapse_whitespace(node.text_content);
        float btn_width = merged.width > 0.0f ? merged.width : (ImGui::CalcTextSize(cleaned_text.c_str()).x + 36.0f);
        float btn_height = merged.height > 0.0f ? merged.height : 0.0f;
        
        ImVec4 btn_bg = merged.has_bg ? merged.bg_color : ImVec4(0.53f, 0.34f, 0.84f, 0.70f);
        ImVec4 btn_text = merged.has_color ? merged.color : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Button, btn_bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(btn_bg.x * 0.95f, btn_bg.y * 0.95f, btn_bg.z * 0.95f, btn_bg.w));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(btn_bg.x * 0.9f, btn_bg.y * 0.9f, btn_bg.z * 0.9f, btn_bg.w));
        ImGui::PushStyleColor(ImGuiCol_Text, btn_text);
        
        float rounding = merged.border_radius >= 0.0f ? merged.border_radius : 0.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, merged.border_width >= 0.0f ? merged.border_width : 0.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Border, merged.has_border_color ? merged.border_color : ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        
        std::string btn_id = cleaned_text + "##" + (node.id.empty() ? std::to_string((uintptr_t)&node) : node.id);
        if (ImGui::Button(btn_id.c_str(), ImVec2(btn_width, btn_height))) {
            if (!node.onclick.empty()) {
                tab.alert_text = extract_alert_message(node.onclick);
                tab.show_alert = true;
            } else {
                tab.alert_text = "Button clicked.";
                tab.show_alert = true;
            }
        }
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    } else if (node.tag == "img") {
        std::string absolute_src = node.src;
        if (absolute_src.find("://") == std::string::npos) {
            absolute_src = resolve_url(tab.current_url, node.src);
        }
        
        auto tex_it = tab.page_textures.find(absolute_src);
        if (tex_it != tab.page_textures.end() && tex_it->second.id != 0) {
            const auto& tex = tex_it->second;
            
            float w = merged.width > 0.0f ? merged.width : (float)tex.width;
            float h = merged.height > 0.0f ? merged.height : (float)tex.height;
            
            float avail_width = ImGui::GetContentRegionAvail().x - (parent_accumulated_right + merged.margin_right + merged.padding_right);
            if (avail_width < 0.0f) avail_width = 0.0f;
            if (w > avail_width && avail_width > 0.0f) {
                float ratio = h / w;
                w = avail_width;
                h = w * ratio;
            }
            
            ImGui::Image((void*)(intptr_t)tex.id, ImVec2(w, h));
        } else {
            ImGui::Button("[Image Missing]", ImVec2(100.0f, 100.0f));
        }
    } else if (node.tag == "a") {
        std::string cleaned_text = collapse_whitespace(node.text_content);
        
        ImVec4 link_color = ImVec4(0.0f, 0.0f, 238.0f / 255.0f, 1.0f);
        auto tag_rule = tab.css_classes.find("a");
        if (tag_rule != tab.css_classes.end() && tag_rule->second.has_color) {
            link_color = tag_rule->second.color;
        }
        if (!node.class_name.empty()) {
            auto class_rule = tab.css_classes.find("." + node.class_name);
            if (class_rule != tab.css_classes.end() && class_rule->second.has_color) {
                link_color = class_rule->second.color;
            }
        }
        if (node.has_inline_style && node.parsed_inline_style.has_color) {
            link_color = node.parsed_inline_style.color;
        }
        
        ImGui::PushStyleColor(ImGuiCol_Text, link_color);
        ImGui::Text("%s", cleaned_text.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImVec2 min_pos = ImGui::GetItemRectMin();
            ImVec2 max_pos = ImGui::GetItemRectMax();
            min_pos.y = max_pos.y;
            ImGui::GetWindowDrawList()->AddLine(min_pos, max_pos, ImGui::ColorConvertFloat4ToU32(link_color));
            
            if (ImGui::IsItemClicked()) {
                std::string new_url = resolve_url(tab.current_url, node.href);
                start_async_fetch(tab.id, new_url);
            }
        }
        ImGui::PopStyleColor();
    } else if (node.tag == "hr") {
        ImGui::Separator();
        ImGui::Spacing();
    } else if (node.tag == "input") {
        std::string type = node.type;
        if (type.empty() || type == "text" || type == "password") {
            char buf[1024] = {0};
            std::strncpy(buf, node.value.c_str(), sizeof(buf) - 1);
            
            float width = merged.width > 0.0f ? merged.width : 200.0f;
            ImGui::PushItemWidth(width);
            
            ImGuiInputTextFlags flags = 0;
            if (node.type == "password") {
                flags |= ImGuiInputTextFlags_Password;
            }
            
            std::string input_label = "##" + (node.id.empty() ? std::to_string((uintptr_t)&node) : node.id);
            
            {
                InputStyleGuard style_guard(merged);
                if (ImGui::InputTextWithHint(input_label.c_str(), node.placeholder.c_str(), buf, sizeof(buf), flags)) {
                    node.value = buf;
                }
            }
            ImGui::PopItemWidth();
        }
    } else if (node.tag == "textarea") {
        char buf[4096] = {0};
        std::strncpy(buf, node.value.c_str(), sizeof(buf) - 1);
        
        float width = merged.width > 0.0f ? merged.width : 300.0f;
        float height = merged.height > 0.0f ? merged.height : 100.0f;
        
        std::string label = "##" + (node.id.empty() ? std::to_string((uintptr_t)&node) : node.id);
        
        {
            InputStyleGuard style_guard(merged);
            if (ImGui::InputTextMultiline(label.c_str(), buf, sizeof(buf), ImVec2(width, height))) {
                node.value = buf;
            }
            
            if (node.value.empty() && !node.placeholder.empty()) {
                ImVec2 min_pos = ImGui::GetItemRectMin();
                ImVec2 max_pos = ImGui::GetItemRectMax();
                float border_size = ImGui::GetStyle().FrameBorderSize;
                ImVec2 clip_min = ImVec2(min_pos.x + border_size, min_pos.y + border_size);
                ImVec2 clip_max = ImVec2(max_pos.x - border_size, max_pos.y - border_size);
                ImVec2 text_pos = ImVec2(min_pos.x + ImGui::GetStyle().FramePadding.x, min_pos.y + ImGui::GetStyle().FramePadding.y);
                
                ImGui::PushClipRect(clip_min, clip_max, true);
                ImGui::GetWindowDrawList()->AddText(
                    ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    text_pos,
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    node.placeholder.c_str(),
                    nullptr,
                    width - ImGui::GetStyle().FramePadding.x * 2.0f
                );
                ImGui::PopClipRect();
            }
        }
    } else if (node.tag == "select") {
        std::vector<std::string> options;
        std::vector<std::string> option_vals;
        int current_item = -1;
        
        for (size_t idx = 0; idx < node.children.size(); idx++) {
            if (node.children[idx].tag == "option") {
                std::string opt_text = trim_spaces(node.children[idx].text_content);
                std::string opt_val = node.children[idx].value.empty() ? opt_text : node.children[idx].value;
                options.push_back(opt_text);
                option_vals.push_back(opt_val);
                
                if (node.value == opt_val) {
                    current_item = (int)idx;
                }
            }
        }
        
        if (current_item == -1 && !option_vals.empty()) {
            current_item = 0;
            node.value = option_vals[0];
        }
        
        std::string combo_label = "##" + (node.id.empty() ? std::to_string((uintptr_t)&node) : node.id);
        
        std::vector<const char*> items;
        for (const auto& opt : options) {
            items.push_back(opt.c_str());
        }
        
        float width = merged.width > 0.0f ? merged.width : 150.0f;
        ImGui::PushItemWidth(width);
        
        {
            InputStyleGuard style_guard(merged);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.53f, 0.34f, 0.84f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.53f, 0.34f, 0.84f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.43f, 0.24f, 0.74f, 1.00f));
            
            if (!items.empty()) {
                if (ImGui::Combo(combo_label.c_str(), &current_item, items.data(), items.size())) {
                    if (current_item >= 0 && current_item < (int)option_vals.size()) {
                        node.value = option_vals[current_item];
                    }
                }
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::PopItemWidth();
    } else if (node.tag == "video") {
        std::string absolute_src = get_media_source(node);
        if (absolute_src.find("://") == std::string::npos) {
            absolute_src = resolve_url(tab.current_url, absolute_src);
        }
        
        std::string cache_path = get_cache_filepath(absolute_src);
        
        VideoPlayer* player = nullptr;
        auto player_it = tab.active_players.find(absolute_src);
        if (player_it == tab.active_players.end()) {
            if (std::filesystem::exists(cache_path)) {
                player = new VideoPlayer(cache_path, false);
                if (node.loop) player->set_loop(true);
                if (node.muted) player->set_muted(true);
                if (node.autoplay) {
                    player->play();
                }
                tab.active_players[absolute_src] = player;
            }
        } else {
            player = player_it->second;
        }

        float w = merged.width > 0.0f ? merged.width : 500.0f;
        float h = merged.height > 0.0f ? merged.height : 375.0f;
        
        float avail_width = ImGui::GetContentRegionAvail().x - (parent_accumulated_right + merged.margin_right + merged.padding_right);
        if (avail_width < 0.0f) avail_width = 0.0f;
        if (w > avail_width && avail_width > 0.0f) {
            float ratio = h / w;
            w = avail_width;
            h = w * ratio;
        }

        std::string id_suffix = "##video_" + std::to_string((uintptr_t)&node);

        if (!player) {
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(w, h));
            ImVec2 center = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                                   (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
            DrawSpinner(center, 25.0f, 3.5f, Theme::spinner);
            ImGui::EndGroup();
        } else {
            player->update();
            
            ImGui::BeginGroup();
            ImVec2 video_pos = ImGui::GetCursorScreenPos();
            
            unsigned int tex_id = player->get_texture_id();
            if (tex_id != 0 && player->get_width() > 0 && player->get_height() > 0) {
                ImGui::Image((void*)(intptr_t)tex_id, ImVec2(w, h));
            } else {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 video_max = ImVec2(video_pos.x + w, video_pos.y + h);
                draw_list->AddRectFilled(video_pos, video_max, IM_COL32(10, 10, 12, 255));
                ImVec2 center = ImVec2(video_pos.x + w * 0.5f, video_pos.y + h * 0.5f);
                DrawSpinner(center, 20.0f, 3.0f, Theme::spinner);
                ImGui::Dummy(ImVec2(w, h));
            }
            
            if (node.controls) {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                float control_bar_height = 42.0f;
                ImVec2 bar_min = ImVec2(video_pos.x, video_pos.y + h - control_bar_height);
                ImVec2 bar_max = ImVec2(video_pos.x + w, video_pos.y + h);

                // Sleek, semi-transparent bar matching Chrome's player (square corners, flush with the frame).
                draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(15, 15, 18, 220));
                // Subtle top separator (horizontal only, so it never spills past the video's sides).
                draw_list->AddLine(bar_min, ImVec2(bar_max.x, bar_min.y), IM_COL32(255, 255, 255, 15), 1.0f);

                DrawMediaControlBar(draw_list, player, bar_min, bar_max, id_suffix, 11.0f);
            }
            ImGui::EndGroup();
            
            // Advance layout cursor to end of video bounds
            ImGui::SetCursorScreenPos(ImVec2(video_pos.x, video_pos.y + h + 8.0f));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    } else if (node.tag == "audio") {
        std::string absolute_src = get_media_source(node);
        if (absolute_src.find("://") == std::string::npos) {
            absolute_src = resolve_url(tab.current_url, absolute_src);
        }
        
        std::string cache_path = get_cache_filepath(absolute_src);
        
        VideoPlayer* player = nullptr;
        auto player_it = tab.active_players.find(absolute_src);
        if (player_it == tab.active_players.end()) {
            if (std::filesystem::exists(cache_path)) {
                player = new VideoPlayer(cache_path, true);
                if (node.loop) player->set_loop(true);
                if (node.muted) player->set_muted(true);
                if (node.autoplay) {
                    player->play();
                }
                tab.active_players[absolute_src] = player;
            }
        } else {
            player = player_it->second;
        }

        float w = merged.width > 0.0f ? merged.width : 450.0f;
        float h = 42.0f;

        std::string id_suffix = "##audio_" + std::to_string((uintptr_t)&node);

        if (!player) {
            ImGui::BeginGroup();
            ImGui::Button("[Audio Loading...]", ImVec2(w, h));
            ImGui::EndGroup();
        } else {
            player->update();
            
            ImGui::BeginGroup();
            ImVec2 audio_pos = ImGui::GetCursorScreenPos();
            
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 card_max = ImVec2(audio_pos.x + w, audio_pos.y + h);
            
            // Draw Chromium style capsule pill
            draw_list->AddRectFilled(audio_pos, card_max, IM_COL32(40, 40, 42, 255), h * 0.5f);
            draw_list->AddRect(audio_pos, card_max, IM_COL32(255, 255, 255, 15), h * 0.5f, 0, 1.0f);
            
            // Chrome-style control bar laid out within the pill bounds.
            DrawMediaControlBar(draw_list, player, audio_pos, card_max, id_suffix, 12.0f);
            
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(ImVec2(audio_pos.x, audio_pos.y + h + 8.0f));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    } else {
        bool child_inline_flow = false;
        float child_accumulated_right = parent_accumulated_right + merged.margin_right + merged.padding_right;
        for (auto& child : node.children) {
            render_node(child, merged, child_inline_flow, tab, -1, child_accumulated_right);
        }
    }

    ImGui::EndGroup();

    if (draw_bg) {
        ImVec2 min_p = content_start;
        ImVec2 max_p = ImGui::GetItemRectMax();
        
        max_p.x += merged.padding_right;
        max_p.y += merged.padding_bottom;
        
        if (merged.width > 0.0f) max_p.x = min_p.x + merged.width;
        if (merged.height > 0.0f) max_p.y = min_p.y + merged.height;
        
        splitter.SetCurrentChannel(draw_list, 0);
        
        float rounding = merged.border_radius;
        if (merged.has_gradient) {
            ImU32 col_start = ImGui::ColorConvertFloat4ToU32(merged.gradient_start);
            ImU32 col_end = ImGui::ColorConvertFloat4ToU32(merged.gradient_end);
            draw_list->AddRectFilledMultiColor(min_p, max_p, col_start, col_start, col_end, col_end);
        } else if (merged.has_bg) {
            draw_list->AddRectFilled(min_p, max_p, ImGui::ColorConvertFloat4ToU32(merged.bg_color), rounding);
        }
        
        if (merged.border_width > 0.0f && merged.has_border_color) {
            draw_list->AddRect(min_p, max_p, ImGui::ColorConvertFloat4ToU32(merged.border_color), rounding, 0, merged.border_width);
        }
        
        splitter.Merge(draw_list);
        
        ImGui::SetCursorScreenPos(ImVec2(start_pos.x, max_p.y + merged.margin_bottom));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    } else {
        bool is_widget = (node.tag == "input" || node.tag == "textarea" || node.tag == "select" || node.tag == "button");
        if (!is_inline && !is_widget && merged.padding_bottom > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.padding_bottom);
        if (!is_inline && merged.margin_bottom > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + merged.margin_bottom);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    if (base_font_scale != 1.0f) {
        ImGui::SetWindowFontScale(1.0f);
    }
}

void DrawSpinner(ImVec2 center, float radius, float thickness, const ImVec4& color) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    int num_segments = 30;
    float start_angle = (float)ImGui::GetTime() * 8.0f;
    float end_angle = start_angle + (3.14159265f * 1.5f);
    draw_list->PathArcTo(center, radius, start_angle, end_angle, num_segments);
    draw_list->PathStroke(ImGui::ColorConvertFloat4ToU32(color), 0, thickness);
}

void DrawBackArrowIcon(ImVec2 center, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(ImVec2(center.x + 7.0f, center.y), ImVec2(center.x - 7.0f, center.y), color, thickness);
    draw_list->PathClear();
    draw_list->PathLineTo(ImVec2(center.x, center.y + 7.0f));
    draw_list->PathLineTo(ImVec2(center.x - 7.0f, center.y));
    draw_list->PathLineTo(ImVec2(center.x, center.y - 7.0f));
    draw_list->PathStroke(color, 0, thickness);
}

void DrawForwardArrowIcon(ImVec2 center, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(ImVec2(center.x - 7.0f, center.y), ImVec2(center.x + 7.0f, center.y), color, thickness);
    draw_list->PathClear();
    draw_list->PathLineTo(ImVec2(center.x, center.y - 7.0f));
    draw_list->PathLineTo(ImVec2(center.x + 7.0f, center.y));
    draw_list->PathLineTo(ImVec2(center.x, center.y + 7.0f));
    draw_list->PathStroke(color, 0, thickness);
}

void DrawReloadIcon(ImVec2 center, float radius, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float PI = 3.14159265f;
    float s = radius / 9.0f;
    
    draw_list->PathArcTo(center, radius, 0.0f, 1.85f * PI, 32);
    draw_list->PathStroke(color, 0, thickness);
    
    draw_list->PathClear();
    draw_list->PathLineTo(ImVec2(center.x + radius, center.y - radius));
    draw_list->PathLineTo(ImVec2(center.x + radius, center.y - 4.0f * s));
    draw_list->PathLineTo(ImVec2(center.x + 4.0f * s, center.y - 4.0f * s));
    draw_list->PathStroke(color, 0, thickness);
}
