// What the overlay that shows the clone thinking puts on screen, worked out from a Look's trace (Brain::lastLook,
// ModelBrain::odds): where the fight is on the match map, why the clone plays what it does, what the rows it weighed cost, and
// what kinds of move a model brain would make next. Pure: the Fetch overlay (ui/clone_overlay) only draws it. docs/CLONE.md.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "hsm_clone.h"
#include "hsm_clone_drill.h"
#include "hsm_model.h"

namespace hsm::clone {

constexpr float kMapMetres = 6.f;     // the match map's far edge: a foe further than this is drawn on it
constexpr int kSourceColours = 6;     // the colours a merged clone's players are told apart by, then again from the first
constexpr size_t kOddsShown = 5;      // the kinds of move a model brain's view lists

// A spot on the match map, both 0..1: x how far the foe is (0 to kMapMetres), y which way (0: 180 degrees to the left, .5
// straight ahead, 1: 180 to the right). Not a number: the middle of the map's edge.
struct MapPoint { float x = 0, y = .5f; };
inline MapPoint MapOf(float metres, float deg) {
    MapPoint p;
    if (std::isfinite(metres)) p.x = std::clamp(metres / kMapMetres, 0.f, 1.f);
    if (std::isfinite(deg)) p.y = std::clamp((deg + 180.f) / 360.f, 0.f, 1.f);
    return p;
}
// Where a recorded row's fight was: how far the foe was from the player, and which way.
inline MapPoint MapOfRow(const float* row) { return MapOf(FoeMetres(row), FoeBearingDeg(row)); }

inline const char* ReasonText(LookWhy w) {
    switch (w) {
        case LookWhy::BestMatch: return "best match";
        case LookWhy::KeptPlaying: return "kept playing";
        case LookWhy::Facing: return "turning to face";
        default: return "";
    }
}

// What a cost bar is marked with: the row ahead of the move playing (it costs kStay of what it would), a move picked again
// within kRepeatMs (kRepeatCost times), and one the clone was hurt playing (kFailCost times).
enum class BarMark : uint8_t { None, Stay, Repeat, Failed };
inline const char* MarkText(BarMark m) {
    switch (m) {
        case BarMark::Stay: return "75% stay";
        case BarMark::Repeat: return "x2 again";
        case BarMark::Failed: return "x4 hurt";
        default: return "";
    }
}
inline BarMark MarkOf(float recall, bool stay) {
    if (recall >= kFailCost) return BarMark::Failed;
    if (recall >= kRepeatCost) return BarMark::Repeat;
    return stay ? BarMark::Stay : BarMark::None;
}
// One row a Look weighed. share: its cost over the dearest one shown (0..1), so the bars are drawn to the same scale.
struct CostBar { uint32_t row = 0; float cost = 0, share = 0; BarMark mark = BarMark::None; bool stay = false, picked = false; };

// The rows a Look weighed, cheapest first, at most kTraceTop: the stay row at what it cost with its bonus, and the cheapest of
// the rest. none: Db::rows(). The one picked is marked; when the clone turns to face the foe, none is.
inline std::vector<CostBar> CostBars(const LookTrace& t, uint32_t none) {
    std::vector<CostBar> bars;
    const NearTrace& n = t.weighed;
    const bool played = t.why == LookWhy::BestMatch || t.why == LookWhy::KeptPlaying;
    if (n.stay != none) bars.push_back({n.stay, n.stayCost, 0, MarkOf(n.stayRecall, true), true, played && t.row == n.stay});
    for (int i = 0; i < n.n && i < kTraceTop; ++i) {
        const NearTrace::Top& top = n.top[size_t(i)];
        if (top.row == n.stay && n.stay != none) continue;   // shown once, at its stay cost
        bars.push_back({top.row, top.cost, 0, MarkOf(top.recall, false), false, played && t.row == top.row});
    }
    std::stable_sort(bars.begin(), bars.end(), [](const CostBar& a, const CostBar& b) { return a.cost < b.cost; });
    if (bars.size() > size_t(kTraceTop)) bars.resize(size_t(kTraceTop));
    float worst = 0;
    for (const CostBar& b : bars) if (std::isfinite(b.cost)) worst = std::max(worst, b.cost);
    for (CostBar& b : bars) b.share = worst > 0 && std::isfinite(b.cost) ? std::clamp(b.cost / worst, 0.f, 1.f) : 1.f;
    return bars;
}

// The inputs a move holds, as the seven of Features 35..41 (or a model clip's bits): W, S, A and D, the right and left
// guard, and run. "still": none.
inline std::string KeysLabel(const float* bits) {
    std::string keys;
    static const char* const k[] = {"W", "S", "A", "D"};
    for (int i = 0; i < 4; ++i) if (bits[i] > .5f) keys += (keys.empty() ? "" : "+") + std::string(k[i]);
    const bool r = bits[4] > .5f, l = bits[5] > .5f;
    std::string o = keys;
    if (r || l) o += (o.empty() ? "" : " ") + std::string(r && l ? "guard" : r ? "guard R" : "guard L");
    if (bits[6] > .5f) o += (o.empty() ? "" : " ") + std::string("run");
    return o.empty() ? "still" : o;
}
// What a recorded row's move was: the inputs held at it and the weapon in hand.
inline std::string MoveLabel(const float* row) { return KeysLabel(row + 35) + ", " + GroupName(WeaponGroupOf(row)); }
inline std::string ClipLabel(const ModelClip& c) { return KeysLabel(c.bit) + ", " + GroupName(c.group); }

// A model brain's next move, as kinds: the odds of tokens whose clips look alike added up. picked: the kind it played.
struct MoveOdds { std::string label; float p = 0; bool picked = false; };
inline std::vector<MoveOdds> MergedOdds(const Model& m, const std::vector<ModelOdd>& odds, int pickedClip, size_t shown = kOddsShown) {
    std::vector<MoveOdds> o;
    const std::string picked = pickedClip >= 0 && uint32_t(pickedClip) < m.clips() ? ClipLabel(m.clip(uint32_t(pickedClip))) : std::string();
    for (const ModelOdd& d : odds) {
        if (d.clip < 0 || uint32_t(d.clip) >= m.clips() || !std::isfinite(d.p)) continue;
        const std::string label = ClipLabel(m.clip(uint32_t(d.clip)));
        auto it = std::find_if(o.begin(), o.end(), [&](const MoveOdds& x) { return x.label == label; });
        if (it == o.end()) o.push_back({label, d.p, label == picked});
        else it->p += d.p;
    }
    std::stable_sort(o.begin(), o.end(), [](const MoveOdds& a, const MoveOdds& b) { return a.p > b.p; });
    if (o.size() > shown) o.resize(shown);
    return o;
}

// The colour a source (Db::SourceOf) is drawn in.
inline int SourceColour(uint32_t source) { return int(source % uint32_t(kSourceColours)); }

// "1.4 s": how far into its move the clone is.
inline std::string ClipText(float ms) {
    if (!std::isfinite(ms) || ms < 0) ms = 0;
    char b[24];
    std::snprintf(b, sizeof b, "%.1f s", double(std::min(ms, 3.6e6f)) / 1000.0);
    return b;
}

}  // namespace hsm::clone
