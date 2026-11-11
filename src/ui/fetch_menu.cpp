#include "ui/fetch_menu.h"
#include "match/hsm_hud_style.h"
#include "ui/umg.h"
#include "log.h"
#include <windows.h>

namespace fetch::ui {
using namespace hsm;
using namespace hsm::ue;
using namespace hsm::ui;
using namespace hsm::style;
namespace clone = hsm::clone;

namespace {
constexpr ULONGLONG kScanEveryMs = 1000;
constexpr int kZOrder = 70;            // over the HUD's own widgets
constexpr float kWidth = 480;

struct Line { Box row; Text label, value; };
struct Widgets {
    UClass* hudClass = nullptr;
    UObject* hud = nullptr; int32_t hudIndex = -1;
    ULONGLONG nextScan = 0, nextLive = 0;
    bool built = false, ok = false;
    Kit kit;
    Fixed box;
    std::array<Line, clone::kMenuLines> rows;
    std::array<Text, kStatusLines> status;
    Text title, hint;
    Fixed bannerBox;
    Text banner;
    std::string bannerShown;
} w;
uint32_t g_builds = 0;   // HudKit: one more each time the widgets are made on a HUD

Text MakeText(Box& column, float size, Rgba color, Margin pad) {
    Text t = w.kit.MakeText();
    if (!t) return t;
    t.Font(size, 1, {0, 0, 0, 0.9f});
    t.Color(color);
    t.Align(Justify::Left);
    column.Add(t).Align(HAlign::Fill, VAlign::Top).Padding(pad);
    return t;
}

bool Build() {
    w.built = true;
    ++g_builds;
    w.kit = Kit(w.hud, Vis::HitTestInvisible);   // keys only: nothing here takes the mouse
    Canvas root = w.kit.Root();
    if (!root) { HSM_LOG("fetch: menu: UI_HUD root is not a CanvasPanel; no menu"); return false; }
    if (!ProbeFont(w.kit, w.hud)) HSM_LOG("fetch: menu: no font could be copied from the game; the menu uses the engine's own");
    w.box = w.kit.MakeFixed();
    w.box.Width(kWidth);
    root.Add(w.box).Anchor({{0.5, 0.5}, {0.5, 0.5}}, {0.5, 0.5}).AutoSize().Z(kZOrder);
    Panel frame = w.kit.MakePanel();
    frame.Color(kMenuAccent);
    frame.Padding({2, 2, 2, 2});
    w.box.Content(frame).Align(HAlign::Fill, VAlign::Top);
    Panel back = w.kit.MakePanel();
    back.Color(kMenuBg);
    back.Padding({18, 12, 18, 14});
    frame.Content(back).Align(HAlign::Fill, VAlign::Fill);
    Box column = w.kit.MakeColumn();
    back.Content(column).Align(HAlign::Fill, VAlign::Top);
    MakeText(column, 24, kMenuAccentInk, {0, 0, 0, 0}).Set("FETCH");
    w.title = MakeText(column, 13, kMenuDim, {0, 0, 0, 10});
    if (!w.title) { HSM_LOG("fetch: menu: could not make the widgets"); return false; }
    for (int r = 0; r < clone::kMenuLines; ++r) {
        Box row = w.kit.MakeRow();
        if (!row) { HSM_LOG("fetch: menu: could not make the widgets"); return false; }
        column.Add(row).Align(HAlign::Fill, VAlign::Top).Padding({0, 3, 0, 3});
        Line& l = w.rows[size_t(r)];
        l.row = row;
        l.label = w.kit.MakeText();
        l.value = w.kit.MakeText();
        if (!l.label || !l.value) { HSM_LOG("fetch: menu: could not make the widgets"); return false; }
        for (Text* t : {&l.label, &l.value}) t->Font(17, 1, {0, 0, 0, 0.9f});
        l.value.Align(Justify::Right);
        row.Add(l.label).Fill(1).Align(HAlign::Fill, VAlign::Center);
        row.Add(l.value).Auto().Align(HAlign::Right, VAlign::Center).Padding({12, 0, 0, 0});
    }
    for (int i = 0; i < kStatusLines; ++i) w.status[size_t(i)] = MakeText(column, 14, kMenuText, {0, i == 0 ? 12.f : 1.f, 0, 1});
    w.hint = MakeText(column, 13, kMenuDim, {0, 10, 0, 0});
    if (!w.hint) { HSM_LOG("fetch: menu: could not make the widgets"); return false; }
    w.box.Show(false);
    // The banner: a strip at the top centre, over the HUD, under the menu.
    w.bannerBox = w.kit.MakeFixed();
    w.bannerBox.Width(720);
    root.Add(w.bannerBox).Anchor({{0.5, 0.0}, {0.5, 0.0}}, {0.5, 0.0}).Position({0, 24}).AutoSize().Z(kZOrder - 1);
    Panel strip = w.kit.MakePanel();
    strip.Color(kMenuBg);
    strip.Padding({16, 8, 16, 8});
    w.bannerBox.Content(strip).Align(HAlign::Fill, VAlign::Top);
    w.banner = w.kit.MakeText();
    if (w.banner) {
        w.banner.Font(18, 1, {0, 0, 0, 0.9f});
        w.banner.Color(kMenuAccentInk);
        w.banner.Align(Justify::Center);
        strip.Content(w.banner).Align(HAlign::Fill, VAlign::Center);
    }
    w.bannerBox.Show(false);
    HSM_LOG("fetch: menu: added to UI_HUD");
    return true;
}

void Reset() { UClass* cls = w.hudClass; w = Widgets{}; w.hudClass = cls; }

// Sits on the UI_HUD that is on screen: the game swaps it on a respawn or a new round, and the widgets go with the old one.
bool Attach() {
    const ULONGLONG now = GetTickCount64();
    if (!w.hudClass) {
        if (now < w.nextScan) return false;
        if (!(w.hudClass = FindClass("UI_HUD_C"))) { w.nextScan = now + kScanEveryMs; return false; }
    }
    if (!w.hud || ObjectAt(w.hudIndex) != w.hud || ClassOf(w.hud) != w.hudClass) {
        if (w.hud) Reset();
        if (now < w.nextScan) return false;
        w.nextScan = now + kScanEveryMs;
        w.hud = OnScreen(FindInstances("UI_HUD_C"));
        if (!w.hud) return false;
        w.hudIndex = ObjectIndex(w.hud);
    }
    if (now >= w.nextLive) {
        w.nextLive = now + kScanEveryMs;
        if (!InViewport(w.hud)) {
            UObject* on = OnScreen(FindInstances("UI_HUD_C"));
            if (!on) { Reset(); return false; }
            if (on != w.hud) { Reset(); w.hud = on; w.hudIndex = ObjectIndex(on); w.nextLive = now + kScanEveryMs; }
        }
    }
    return true;
}
}  // namespace

bool MenuCanShow() { return Attach(); }

bool HudKit(Kit*& kit, uint32_t& hud) {
    if (!Attach()) return false;
    if (!w.built) w.ok = Build();
    if (!w.ok) return false;
    kit = &w.kit; hud = g_builds;
    return true;
}

bool HudStill(uint32_t hud) { return Attach() && w.built && w.ok && g_builds == hud; }

void MenuTick(bool open, const MenuView& view) {
    if (!open) { if (w.box) w.box.Show(false); return; }
    if (!Attach()) return;
    if (!w.built) w.ok = Build();
    if (!w.ok) return;
    w.box.Show(true);
    w.title.Set(view.title);
    for (size_t r = 0; r < w.rows.size(); ++r) {
        Line& l = w.rows[r];
        l.row.Show(r < view.lines.size());
        if (r >= view.lines.size()) continue;
        const clone::MenuLine& line = view.lines[r];
        l.label.Set((line.chosen ? "> " : "   ") + line.label);
        l.label.Color(line.chosen ? kMenuAccentInk : line.on ? kMenuOk : kMenuText);
        l.value.Set(line.value);
        l.value.Color(line.on && !view.merge ? kMenuOk : line.chosen ? kBone : kMenuText);
    }
    for (int i = 0; i < kStatusLines; ++i) w.status[size_t(i)].Set(view.status[size_t(i)]);
    w.hint.Set(view.typing  ? std::string("Type a name   Backspace: erase   Enter: keep it   Esc: leave it")
               : view.merge ? "W/S or arrows: choose   A/D or Enter: pick   Enter: do   Esc: back   " + view.key + ": close"
                            : "W/S or arrows: choose   A/D: change   Enter: do   " + view.key + " or Esc: close");
}

void BannerTick(const std::string& text) {
    if (text.empty()) {
        if (w.bannerBox && !w.bannerShown.empty()) { w.bannerBox.Show(false); w.bannerShown.clear(); }
        return;
    }
    if (!Attach()) return;
    if (!w.built) w.ok = Build();
    if (!w.ok || !w.banner) return;
    if (text != w.bannerShown) { w.banner.Set(text); if (w.bannerShown.empty()) w.bannerBox.Show(true); w.bannerShown = text; }
}

}  // namespace fetch::ui
