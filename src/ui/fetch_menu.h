// The Fetch menu on screen (; in game): a panel on the HUD that lists the settings, the one chosen, and how the recording and
// the clone are doing. What the keys do is shared/clone/hsm_fetch_menu.h; this only draws it. Game thread.
#pragma once
#include "clone/hsm_fetch_menu.h"
#include <array>
#include <string>
#include <vector>

namespace fetch::ui {

constexpr int kStatusLines = 6;

struct MenuView {
    hsm::clone::MenuRow row = hsm::clone::MenuRow::Record;
    std::array<std::string, hsm::clone::kMenuRows> values;
    std::array<std::string, kStatusLines> status;
    std::string key;   // the key that opens and closes it, MenuKeyName
};

// Whether the HUD the menu sits on is on screen now (the game swaps it on a respawn). Looks at most once a second.
bool MenuCanShow();
// Draws the menu (open) or hides it. Widgets are made the first time it opens on each HUD.
void MenuTick(bool open, const MenuView& view);
// A line at the top of the screen (a training session's stage and time left); empty hides it.
void BannerTick(const std::string& text);

}  // namespace fetch::ui
