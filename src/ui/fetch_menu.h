// The Fetch menu on screen (; in game): a panel on the HUD that lists the settings, the one chosen, and how the recording and
// the clone are doing. What the keys do is shared/clone/hsm_fetch_menu.h; this only draws it. Game thread.
#pragma once
#include "clone/hsm_fetch_menu.h"
#include "ui/umg.h"
#include <array>
#include <string>
#include <vector>

namespace fetch::ui {

constexpr int kStatusLines = 6;

struct MenuView {
    std::string title;                        // under FETCH: what the page is
    std::vector<hsm::clone::MenuLine> lines;  // at most kMenuLines (Menu::Lines)
    std::array<std::string, kStatusLines> status;
    std::string key;   // the key that opens and closes it, MenuKeyName
    bool typing = false;   // a name is being typed on the chosen line
    bool merge = false;    // the Merge page
};

// Whether the HUD the menu sits on is on screen now (the game swaps it on a respawn). Looks at most once a second.
bool MenuCanShow();
// Draws the menu (open) or hides it. Widgets are made the first time it opens on each HUD.
void MenuTick(bool open, const MenuView& view);
// The HUD the menu is on, for Fetch's other widgets (the clone overlay): its kit, whose root canvas they go on, and a number that
// changes with each HUD the widgets are made on (those on an old one went with it). False while there is none on screen.
bool HudKit(hsm::ui::Kit*& kit, uint32_t& hud);
// Whether widgets made on HudKit's `hud` are still on screen (else they went with their HUD, and must not be touched).
bool HudStill(uint32_t hud);
// A line at the top of the screen (a training session's stage and time left); empty hides it.
void BannerTick(const std::string& text);

}  // namespace fetch::ui
