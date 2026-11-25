/* The OSDs' look and their controls, on Dear ImGui: text in the OSDs' fonts,
 * the motion, and the widgets the settings OSD is made of (overlay_widgets.h) */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "overlay.h"
#include "overlay_icons.h"
#include "overlay_widgets.h"

/* A colour of the palette; GetColorU32 fades it with the style's alpha (greyed out items) */
ImU32 ui_col(const ImVec4& c, float alpha)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

/* Text in Source Sans 3 at a size, on a draw list, and its size */
ImVec2 ui_text_size(float size, const char* text, float wrap)
{
    return g_font->CalcTextSizeA(size, FLT_MAX, wrap, text);
}

void ui_text(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text, float wrap)
{
    dl->AddText(g_font, size, ImFloor(pos), col, text, NULL, wrap);
}

/* The text, or as much of it as fits in max_w with an ellipsis (in buf) */
const char* ui_fit(float size, const char* text, float max_w, char* buf, size_t buf_size)
{
    if (ui_text_size(size, text).x <= max_w || buf_size < 8)
        return text;

    static const char ellipsis[] = "\xe2\x80\xa6";
    size_t len = strlen(text);

    if (len > buf_size - sizeof(ellipsis))
        len = buf_size - sizeof(ellipsis);

    while (len > 0)
    {
        memcpy(buf, text, len);
        memcpy(buf + len, ellipsis, sizeof(ellipsis));

        if (ui_text_size(size, buf).x <= max_w)
            return buf;

        /* back a whole character */
        len--;

        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
            len--;
    }

    memcpy(buf, ellipsis, sizeof(ellipsis));
    return buf;
}

/* An icon (an ICON_ string) centred on a point */
void ui_icon(ImDrawList* dl, const char* icon, ImVec2 center, float size, ImU32 col)
{
    if (!g_icons || !icon || !*icon)
        return;

    unsigned int cp = 0;
    ImTextCharFromUtf8(&cp, icon, NULL);

    ImFontBaked* baked = g_icons->GetFontBaked(size);
    const ImFontGlyph* glyph = baked ? baked->FindGlyphNoFallback((ImWchar)cp) : NULL;

    if (!glyph)
        return;

    float k = baked->Size > 0 ? size / baked->Size : 1.0f;
    ImVec2 pos(center.x - (glyph->X0 + glyph->X1) * 0.5f * k, center.y - (glyph->Y0 + glyph->Y1) * 0.5f * k);
    dl->AddText(g_icons, size, ImVec2(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f)), col, icon, icon + strlen(icon));
}

/* A switch, on or off, at pos */
void ui_switch(ImDrawList* dl, ImVec2 pos, float w, float h, bool on, bool hovered)
{
    float r = h * 0.5f;
    ImVec4 track = on ? COL_ACCENT : COL_TRACK;

    if (hovered)
        track = on ? COL_ACCENT_HI : ImVec4(0.36f, 0.37f, 0.40f, 1.0f);

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), ui_col(track), r);

    float knob = r - (h * 0.14f > 1.5f ? h * 0.14f : 1.5f);
    ImVec2 c(on ? pos.x + w - r : pos.x + r, pos.y + r);
    dl->AddCircleFilled(c, knob, ui_col(COL_KNOB), 24);
}

/* ------------------------------------------------------------ the motion */

/* The frame's time step, for the animations: a long frame doesn't skip them */
float ui_dt(void)
{
    float dt = ImGui::GetIO().DeltaTime;
    return dt < 0.0f ? 0.0f : dt > 0.1f ? 0.1f : dt;
}

/* Something of the OSD moved this frame: a change waits until nothing does */
bool g_ui_moving;

/* x moved toward target by a part of the way that depends on the time, not
 * the frame rate; within snap of it, it's there. min_rate = the least it
 * moves a second, so it doesn't crawl the last of the way */
float ui_approach(float x, float target, float speed, float snap, float min_rate)
{
    float d = target - x;
    float step = d * (1.0f - expf(-ui_dt() * speed));
    float least = ui_dt() * min_rate;

    if (fabsf(step) < least)
        step = fabsf(d) < least ? d : d > 0.0f ? least : -least;

    x += step;

    if (fabsf(target - x) < snap)
        x = target;

    if (x != target)
        g_ui_moving = true;

    return x;
}

/* A rectangle gliding to target: the highlight of a row, the pill of a
 * choice. It's kept relative to origin (where the window's content starts),
 * so it doesn't glide when the window scrolls. snap = jump there; it jumps
 * too when it wasn't there the frame before (a list opened again, its rows
 * moved since), so it never glides in from where it was long ago */
ImRect ui_slide(ImGuiID id, const ImRect& target, ImVec2 origin, bool snap)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID frame_key = ImHashStr("##frame", 0, id);
    int frame = ImGui::GetFrameCount();
    float to[4] = { target.Min.x - origin.x, target.Min.y - origin.y, target.Max.x - origin.x, target.Max.y - origin.y };
    float v[4];
    bool known = st->GetBool(id, false) && !snap && st->GetInt(frame_key, -10) >= frame - 1;

    for (int i = 0; i < 4; i++)
    {
        ImGuiID key = ImHashData(&i, sizeof(i), id);
        v[i] = known ? ui_approach(st->GetFloat(key, to[i]), to[i], 18.0f, 0.25f) : to[i];
        st->SetFloat(key, v[i]);
    }

    st->SetBool(id, true);
    st->SetInt(frame_key, frame);
    return ImRect(v[0] + origin.x, v[1] + origin.y, v[2] + origin.x, v[3] + origin.y);
}

/* Where ui_slide left it, held there (a highlight fading out); it stays
 * known, so it glides from there if it's wanted again before it's gone */
ImRect ui_slide_hold(ImGuiID id, ImVec2 origin)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    float v[4];

    for (int i = 0; i < 4; i++)
        v[i] = st->GetFloat(ImHashData(&i, sizeof(i), id), 0.0f);

    st->SetInt(ImHashStr("##frame", 0, id), ImGui::GetFrameCount());
    return ImRect(v[0] + origin.x, v[1] + origin.y, v[2] + origin.x, v[3] + origin.y);
}

/* A chevron pointing right, turned by angle (radians) clockwise */
void ui_chevron(ImDrawList* dl, ImVec2 c, float size, float angle, ImU32 col)
{
    float ca = cosf(angle), sa = sinf(angle);
    ImVec2 pts[3] = { ImVec2(-0.17f, -0.34f), ImVec2(0.17f, 0.0f), ImVec2(-0.17f, 0.34f) };

    for (int i = 0; i < 3; i++)
    {
        float x = pts[i].x * size, y = pts[i].y * size;
        pts[i] = ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca);
    }

    float thick = size * 0.12f > 1.5f ? size * 0.12f : 1.5f;
    dl->AddPolyline(pts, 3, col, thick);
}

/* The keyboard is in use: its cursor shows, and the mouse hasn't moved since */
bool ui_keyboard(void)
{
    ImGuiContext& g = *GImGui;
    return g.NavCursorVisible && g.NavHighlightItemUnderNav;
}

bool g_ui_take_focus;

/* The keyboard goes to this item when its row was just opened with the
 * keyboard; called straight after its ItemAdd */
void ui_take_focus(ImGuiID id)
{
    if (!g_ui_take_focus)
        return;

    ImGuiContext& g = *GImGui;
    ImGui::SetFocusID(id, g.CurrentWindow);
    g.NavCursorVisible = true;
    g.NavHighlightItemUnderNav = true;
    g_ui_take_focus = false;
}

/* Choices side by side, the one picked on a pill that glides to the next.
 * Returns the one clicked, or -1 */
int ui_segmented(const char* str_id, const char* const* labels, int n, int selected, float F)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems || n <= 0 || n > 16)
        return -1;

    ImGuiContext& g = *GImGui;
    ImDrawList* dl = window->DrawList;
    ImGui::PushID(str_id);

    float h = floorf(2.1f * F), w = ImGui::GetContentRegionAvail().x;
    float inset = floorf(0.2f * F > 2.0f ? 0.2f * F : 2.0f), r = 0.45f * F;
    float label_size = 0.98f * F;
    ImVec2 pos = window->DC.CursorPos;
    ImRect bar(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bar);
    dl->AddRectFilled(bar.Min, bar.Max, ui_col(COL_PILL), r);

    ImRect segs[16];
    int picked = -1;

    for (int i = 0; i < n; i++)
    {
        ImRect seg(ImVec2(floorf(pos.x + w * i / n), pos.y), ImVec2(floorf(pos.x + w * (i + 1) / n), pos.y + h));
        segs[i] = ImRect(seg.Min + ImVec2(inset, inset), seg.Max - ImVec2(inset, inset));

        ImGuiID id = window->GetID(i);

        bool visible = ImGui::ItemAdd(seg, id);

        if (i == (selected >= 0 ? selected : 0))
            ui_take_focus(id);

        if (!visible)
            continue;

        bool hovered, held;

        if (ImGui::ButtonBehavior(seg, id, &hovered, &held))
            picked = i;

        if (i != selected && (hovered || (g.NavId == id && ui_keyboard())))
            dl->AddRectFilled(segs[i].Min, segs[i].Max, ui_col(held ? COL_ROW_ACTIVE : COL_PILL_HOVER), r - inset);

        ImGui::RenderNavCursor(seg, id, ImGuiNavRenderCursorFlags_None, r);

        if (i == selected)
            ImGui::SetItemDefaultFocus();
    }

    ImGuiID pill_id = window->GetID("##pill");
    ImRect pill;
    bool has_pill = selected >= 0 && selected < n;

    if (has_pill)
    {
        pill = ui_slide(pill_id, segs[selected], window->DC.CursorStartPos, false);
        dl->AddRectFilled(pill.Min, pill.Max, ui_col(COL_ACCENT), r - inset);
    }
    else
    {
        ImGui::GetStateStorage()->SetBool(pill_id, false); /* it appears where it's next put */
    }

    /* The text white as far as the pill is under it */
    static const ImVec4 off(0.80f, 0.81f, 0.83f, 1.0f);
    float vh = ui_text_size(label_size, "A").y;

    for (int i = 0; i < n; i++)
    {
        float k = 0.0f;

        if (has_pill)
        {
            float o = ImMin(pill.Max.x, segs[i].Max.x) - ImMax(pill.Min.x, segs[i].Min.x);
            k = ImClamp(o / (segs[i].GetWidth() > 1.0f ? segs[i].GetWidth() : 1.0f), 0.0f, 1.0f);
        }

        ImVec4 c(off.x + (1.0f - off.x) * k, off.y + (1.0f - off.y) * k, off.z + (1.0f - off.z) * k, 1.0f);
        char buf[MENU_TEXT];
        const char* text = ui_fit(label_size, labels[i], segs[i].GetWidth() - 0.5f * F, buf, sizeof(buf));
        float tw = ui_text_size(label_size, text).x;
        ui_text(dl, label_size, ImVec2((segs[i].Min.x + segs[i].Max.x - tw) * 0.5f, pos.y + (h - vh) * 0.5f), ui_col(c), text);
    }

    ImGui::PopID();
    return picked;
}

/* A button, accent = the one to press */
bool ui_button(const char* label, float w, bool accent)
{
    if (accent)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_HI);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_ACCENT);
    }

    bool pressed = ImGui::Button(label, ImVec2(w, 0));

    if (accent)
        ImGui::PopStyleColor(3);

    return pressed;
}

/* Text in the help's grey, wrapped, a little smaller */
void ui_note(const char* text, float F, const ImVec4& col)
{
    ImGui::PushFont(NULL, 0.9f * F);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/* A box to type a number in, written when it's left. Returns true and the
 * number then */
bool ui_int_field(const char* str_id, int v, int lo, int hi, float w, int* out)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGui::PushID(str_id);
    ImGuiID edited = ImGui::GetID("##edited"), typed = ImGui::GetID("##typed");
    bool commit = false;
    int n = v;

    ImGui::SetNextItemWidth(w);

    if (ImGui::InputInt("##n", &n, 0, 0))
    {
        st->SetBool(edited, true);
        st->SetInt(typed, n);
    }

    if (ImGui::IsItemDeactivated() && st->GetBool(edited))
    {
        st->SetBool(edited, false);

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            n = st->GetInt(typed, v);
            *out = n < lo ? lo : n > hi ? hi : n;
            commit = true;
        }
    }

    ImGui::PopID();
    return commit;
}

/* A label, a slider and a box to type the number in. Returns true and the
 * number when the slider is let go or a number is typed in. The box takes
 * numbers up to box_hi (0 = hi), past the slider's end; live = the number
 * while the slider is dragged, -1 otherwise */
bool ui_slider_box(const char* str_id, const char* label, int v, int lo, int hi, float F, int* out,
                   int box_hi, int* live)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(str_id);
    ImGuiID dragging = ImGui::GetID("##dragging"), dragged = ImGui::GetID("##dragged");
    bool commit = false;
    int cur = st->GetBool(dragging) ? st->GetInt(dragged, v) : v;
    int typed = cur;

    if (label)
    {
        float x0 = ImGui::GetCursorPosX();
        float lw = ImGui::CalcTextSize(label).x + style.ItemSpacing.x * 2;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(x0 + (lw > 7.0f * F ? lw : 7.0f * F));
    }

    float box = ImGui::CalcTextSize("00000").x + style.FramePadding.x * 2;
    float slider = ImGui::GetContentRegionAvail().x - box - style.ItemSpacing.x;

    if (slider < ImGui::GetFrameHeight())
        slider = ImGui::GetFrameHeight();

    ImGui::SetNextItemWidth(slider);

    if (ImGui::SliderInt("##slider", &cur, lo, hi, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        st->SetBool(dragging, true);
        st->SetInt(dragged, cur);
        typed = cur;
    }

    if (ImGui::IsItemDeactivated() && st->GetBool(dragging))
    {
        st->SetBool(dragging, false);
        *out = st->GetInt(dragged, v);
        commit = true;
    }

    if (live)
        *live = st->GetBool(dragging) ? st->GetInt(dragged, v) : -1;

    ImGui::SameLine();

    if (ui_int_field("##box", typed, lo, box_hi > hi ? box_hi : hi, box, out))
        commit = true;

    ImGui::PopID();
    return commit;
}
