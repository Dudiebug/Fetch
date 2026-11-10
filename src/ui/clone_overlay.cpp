#include "ui/clone_overlay.h"
#include "match/hsm_hud_style.h"
#include "ui/fetch_menu.h"
#include "ui/umg.h"
#include "log.h"
#include <algorithm>
#include <array>

namespace fetch::ui {
using namespace hsm;
using namespace hsm::ui;
using namespace hsm::style;
namespace clone = hsm::clone;

namespace {
constexpr int kZOrder = 69;            // over the HUD's own widgets, under the menu
constexpr float kWidth = 320, kMapW = 284, kMapH = 112, kDot = 8, kRing = 14, kFoe = 10;
constexpr float kLabelW = 150, kBarW = 80;

// The colours a merged clone's players are told apart by (SourceColour).
constexpr std::array<Rgba, clone::kSourceColours> kSources = {{
    {0.95f, 0.32f, 0.26f, 1}, {0.40f, 0.55f, 1.0f, 1}, {0.30f, 0.80f, 0.30f, 1},
    {0.85f, 0.62f, 0.05f, 1}, {0.72f, 0.45f, 0.95f, 1}, {0.30f, 0.80f, 0.85f, 1}}};
Rgba SourceRgba(int c) { return c >= 0 && c < clone::kSourceColours ? kSources[size_t(c)] : kMenuAccentInk; }

struct BarRow { Box row; Text label, value; Bar bar; };
struct Dot { Icon icon; Slot slot{nullptr}; };
struct Widgets {
    uint32_t hud = 0;
    bool built = false, ok = false;
    Fixed box;
    Text playing, detail, mapTitle, barsTitle;
    Box legend;
    std::array<Text, clone::kSourceColours> legendNames;
    Fixed map;
    Dot foe, ring;
    std::array<Dot, clone::kTraceTop> dots;
    std::array<BarRow, kMindBars> bars;
    uint32_t seq = 0;
    bool seqKnown = false, shown = false;
} w;

Text MakeText(Kit& kit, Box& column, float size, Rgba color, Margin pad) {
    Text t = kit.MakeText();
    if (!t) return t;
    t.Font(size, 1, {0, 0, 0, 0.9f});
    t.Color(color);
    t.Align(Justify::Left);
    column.Add(t).Align(HAlign::Fill, VAlign::Top).Padding(pad);
    return t;
}

Dot MakeDot(Kit& kit, Box& stack, float size, Rgba color) {
    Dot d;
    d.icon = kit.MakeIcon();
    if (!d.icon) return d;
    d.icon.Color(color);
    d.icon.Size({size, size});
    d.slot = stack.Add(d.icon);
    d.slot.Align(HAlign::Left, VAlign::Top);
    d.icon.Show(false);
    return d;
}

// Where a spot on the map is drawn: which way across (left to right), how far up (the clone at the bottom, kMapMetres at the top).
void Place(Dot& d, clone::MapPoint p, float size) {
    if (!d.icon) return;
    const float x = std::clamp(p.y * kMapW - size / 2, 0.f, kMapW - size), y = std::clamp((1 - p.x) * kMapH - size / 2, 0.f, kMapH - size);
    d.slot.Padding({x, y, 0, 0});
    d.icon.Show(true);
}

bool Build(Kit& kit) {
    w.built = true;
    Canvas root = kit.Root();
    if (!root) return false;
    w.box = kit.MakeFixed();
    if (!w.box) return false;
    w.box.Width(kWidth);
    root.Add(w.box).Anchor({{0, 0.5}, {0, 0.5}}, {0, 0.5}).Position({16, 0}).AutoSize().Z(kZOrder);
    Panel back = kit.MakePanel();
    if (!back) return false;
    back.Color({kMenuBg.r, kMenuBg.g, kMenuBg.b, 0.82f});
    back.Padding({14, 10, 14, 12});
    w.box.Content(back).Align(HAlign::Fill, VAlign::Top);
    Box column = kit.MakeColumn();
    if (!column) return false;
    back.Content(column).Align(HAlign::Fill, VAlign::Top);
    MakeText(kit, column, 14, kMenuAccentInk, {0, 0, 0, 4}).Set("THE CLONE THINKING");
    w.playing = MakeText(kit, column, 17, kBone, {0, 0, 0, 0});
    w.detail = MakeText(kit, column, 13, kMenuText, {0, 0, 0, 6});
    w.legend = kit.MakeRow();
    if (!w.playing || !w.detail || !w.legend) return false;
    column.Add(w.legend).Align(HAlign::Left, VAlign::Top).Padding({0, 0, 0, 4});
    for (int i = 0; i < clone::kSourceColours; ++i) {
        Text& t = w.legendNames[size_t(i)];
        t = kit.MakeText();
        if (!t) return false;
        t.Font(13, 1, {0, 0, 0, 0.9f});
        t.Color(SourceRgba(i));
        w.legend.Add(t).Auto().Padding({0, 0, 10, 0});
    }
    w.legend.Show(false);
    w.mapTitle = MakeText(kit, column, 12, kMenuDim, {0, 2, 0, 2});
    w.mapTitle.Set("Match map: left to right which way the foe is, up how far (6 m). White: now");
    w.mapTitle.Wrap(true);
    w.map = kit.MakeFixed();
    if (!w.map) return false;
    w.map.Width(kMapW);
    w.map.Height(kMapH);
    column.Add(w.map).Align(HAlign::Left, VAlign::Top).Padding({0, 0, 0, 6});
    Box stack = kit.MakeStack();
    if (!stack) return false;
    w.map.Content(stack).Align(HAlign::Fill, VAlign::Fill);
    Icon field = kit.MakeIcon();
    if (!field) return false;
    field.Color(kMenuCard);
    stack.Add(field).Align(HAlign::Fill, VAlign::Fill);
    Icon ahead = kit.MakeIcon();   // straight ahead of the clone
    if (!ahead) return false;
    ahead.Color(kMenuFrameHover);
    ahead.Size({1, kMapH});
    stack.Add(ahead).Align(HAlign::Center, VAlign::Fill);
    w.ring = MakeDot(kit, stack, kRing, kBone);   // under the dot picked
    for (Dot& d : w.dots) d = MakeDot(kit, stack, kDot, kMenuText);
    w.foe = MakeDot(kit, stack, kFoe, {1, 1, 1, 1});
    if (!w.ring.icon || !w.foe.icon || std::any_of(w.dots.begin(), w.dots.end(), [](const Dot& d) { return !d.icon; })) return false;
    w.barsTitle = MakeText(kit, column, 12, kMenuDim, {0, 2, 0, 2});
    if (!w.barsTitle) return false;
    for (BarRow& b : w.bars) {
        b.row = kit.MakeRow();
        b.label = kit.MakeText();
        b.value = kit.MakeText();
        b.bar = kit.MakeBar();
        Fixed labelBox = kit.MakeFixed(), barBox = kit.MakeFixed();
        if (!b.row || !b.label || !b.value || !b.bar || !labelBox || !barBox) return false;
        column.Add(b.row).Align(HAlign::Fill, VAlign::Top).Padding({0, 1, 0, 1});
        for (Text* t : {&b.label, &b.value}) t->Font(12, 1, {0, 0, 0, 0.9f});
        labelBox.Width(kLabelW);
        labelBox.Content(b.label).Align(HAlign::Left, VAlign::Center);
        barBox.Width(kBarW);
        barBox.Height(10);
        barBox.Content(b.bar).Align(HAlign::Fill, VAlign::Fill);
        b.row.Add(labelBox).Auto().Align(HAlign::Left, VAlign::Center);
        b.row.Add(barBox).Auto().Align(HAlign::Left, VAlign::Center).Padding({4, 0, 6, 0});
        b.row.Add(b.value).Fill(1).Align(HAlign::Fill, VAlign::Center);
        b.row.Show(false);
    }
    w.box.Show(false);
    HSM_LOG("fetch: mind: the overlay is on UI_HUD");
    return true;
}

void Draw(const MindView& v) {
    w.playing.Set(v.playing.empty() ? std::string("waiting for its first look") : v.playing);
    w.detail.Set(v.detail);
    w.legend.Show(v.sources.size() > 1 && !v.model);
    for (size_t i = 0; i < w.legendNames.size(); ++i) {
        w.legendNames[i].Show(i < v.sources.size());
        if (i < v.sources.size()) w.legendNames[i].Set(v.sources[i]);
    }
    w.mapTitle.Show(!v.model);
    w.map.Show(!v.model);
    w.barsTitle.Set(v.model ? "What it may do next, and how likely (the one played marked >)"
                            : "What each moment cost: shorter fits better (the one played marked >)");
    if (w.seqKnown && v.seq == w.seq) return;   // the map and bars change only with a look
    w.seq = v.seq; w.seqKnown = true;
    if (!v.model) {
        if (v.foeKnown) Place(w.foe, v.foe, kFoe); else w.foe.icon.Show(false);
        bool ringed = false;
        for (size_t i = 0; i < w.dots.size(); ++i) {
            Dot& d = w.dots[i];
            if (i >= v.dots.size()) { d.icon.Show(false); continue; }
            d.icon.Color(SourceRgba(v.dots[i].colour));
            Place(d, v.dots[i].at, kDot);
            if (v.dots[i].picked && !ringed) { Place(w.ring, v.dots[i].at, kRing); ringed = true; }
        }
        if (!ringed) w.ring.icon.Show(false);
    }
    for (size_t i = 0; i < w.bars.size(); ++i) {
        BarRow& b = w.bars[i];
        b.row.Show(i < v.bars.size());
        if (i >= v.bars.size()) continue;
        const MindBar& m = v.bars[i];
        b.label.Set((m.picked ? "> " : "  ") + m.label);
        b.label.Color(m.picked ? kBone : kMenuText);
        b.value.Set(m.value);
        b.value.Color(m.picked ? kBone : kMenuDim);
        b.bar.Percent(std::clamp(m.share, 0.f, 1.f));
        b.bar.FillColor(m.colour < 0 ? (m.picked ? kMenuAccentInk : kMenuAccent) : SourceRgba(m.colour));
    }
}
}  // namespace

void MindTick(bool show, const MindView& view) {
    if (!show) {
        if (w.shown && HudStill(w.hud)) w.box.Show(false);   // a HUD that went took the widgets with it
        w.shown = false;
        return;
    }
    Kit* kit = nullptr;
    uint32_t hud = 0;
    if (!HudKit(kit, hud)) return;
    if (!w.built || hud != w.hud) { w = Widgets{}; w.hud = hud; w.ok = Build(*kit); if (!w.ok) HSM_LOG("fetch: mind: could not make the overlay's widgets"); }
    if (!w.ok) return;
    w.box.Show(true);
    w.shown = true;
    Draw(view);
}

}  // namespace fetch::ui
