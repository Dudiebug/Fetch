// The overlay that shows the clone thinking (show_mind=1, the menu's "Show the clone thinking"): on the left of the HUD, what the
// clone plays and why, where the fight is on the match map against the moments it weighed, what each of them cost, and for a
// trained model the odds of its next kinds of move. shared/clone/hsm_clone_mind.h works it out; this only draws it. docs/CLONE.md.
// Game thread.
#pragma once
#include "clone/hsm_clone_mind.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fetch::ui {

// A moment the clone weighed, on the match map (hsm_clone_mind.h MapPoint). colour: SourceColour, the player it came from.
struct MindDot { hsm::clone::MapPoint at; int colour = 0; bool picked = false; };
// A bar: a moment's cost (shorter fits better), or a kind of move's odds. share 0..1; colour -1: the accent.
struct MindBar { std::string label, value; float share = 0; int colour = -1; bool picked = false; };

struct MindView {
    uint32_t seq = 0;               // one more each look: the map and bars change only then
    bool model = false;             // a trained model: odds bars, no map
    std::string playing, detail;    // "W+A guard R, longsword"; "from Ana   1.4 s   best match"
    hsm::clone::MapPoint foe;       // where the foe is now
    bool foeKnown = false;
    std::vector<MindDot> dots;      // at most kTraceTop
    std::vector<MindBar> bars;      // at most kMindBars
    std::vector<std::string> sources;   // a merged clone's players, in their colours (at most kSourceColours); one: none shown
};

constexpr int kMindBars = hsm::clone::kTraceTop > int(hsm::clone::kOddsShown) ? hsm::clone::kTraceTop : int(hsm::clone::kOddsShown);

// Draws the overlay (show) or hides it. Made the first time it shows on each HUD.
void MindTick(bool show, const MindView& view);

}  // namespace fetch::ui
