// Training sessions: a run of stages, each a weapon for the player and a kind of foe, so that a clone gets to see how its player
// fights with everything against everything, not only with the one weapon they like against whoever turns up. What a recording
// already holds (Coverage) says what is missing; PlanSession picks the stages. The game dresses each foe by its tier, so a stage
// asks for a tier and the armour it tends to bring; what the foe really wore is read and counted (the Armour events, the rows'
// armour columns). Pure logic: client/Fetch runs a session (training.cpp), docs/CLONE.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "hsm_clone.h"

namespace hsm::clone {

// What a foe wears, as one word: nothing, cloth, mail, plate. From a row's armour columns (Cover7).
enum class FoeArmour : uint8_t { Bare, Cloth, Mail, Plate };
constexpr int kFoeArmours = 4;
inline FoeArmour FoeArmourOf(const float* cover7) {
    int plate = 0, mail = 0, cloth = 0;
    for (int i = 0; i < kParts; ++i) {
        const float c = cover7[i];
        plate += c >= .95f; mail += c >= .7f && c < .95f; cloth += c >= .4f && c < .7f;
    }
    if (plate >= 3) return FoeArmour::Plate;
    if (plate + mail >= 2) return FoeArmour::Mail;
    return plate + mail + cloth >= 2 ? FoeArmour::Cloth : FoeArmour::Bare;
}
inline const char* FoeArmourName(FoeArmour a) { static const char* const n[] = {"bare", "cloth", "mail", "plate"}; return n[size_t(a) & 3u]; }

// The weapons a stage hands the player: the game's seven types (WeaponGroup 1..7).
constexpr uint8_t kFirstDrillGroup = 1, kLastDrillGroup = 7;
constexpr int kDrillGroups = kLastDrillGroup - kFirstDrillGroup + 1;
inline const char* GroupName(uint8_t g) {
    static const char* const n[] = {"no weapon", "one-handed sword", "two-handed sword", "one-handed mace", "two-handed mace",
                                    "one-handed axe or hammer", "two-handed axe or hammer", "polearm", "short weapon", "weapon"};
    return g < kWeaponGroups ? n[g] : "?";
}
// The tier a stage asks the game for, for the armour it wants the foe in (kTierBudget: what the generator may spend on gear).
inline uint8_t TierFor(FoeArmour a) { static const uint8_t t[] = {0, 2, 4, 6}; return t[size_t(a) & 3u]; }

// Seconds fought, by the player's weapon group and the foe's armour, over what a recording holds (rows whose armour is known).
struct Coverage {
    std::array<std::array<float, kFoeArmours>, kWeaponGroups> s{};
    float total = 0;
    void Add(uint8_t group, FoeArmour a, float seconds) {
        if (group >= kWeaponGroups || !(seconds > 0)) return;
        s[group][size_t(a)] += seconds; total += seconds;
    }
    float at(uint8_t group, FoeArmour a) const { return group < kWeaponGroups ? s[group][size_t(a)] : 0.f; }
};
inline Coverage CoverageOf(const Db& db) {
    Coverage c;
    for (uint32_t r = 0; r < db.rows(); ++r) {
        const float* f = db.row(r);
        if (f[kArmorKnown] < .5f) continue;
        const uint32_t from = db.start(r), to = r + 1 < db.rows() ? std::max(db.start(r + 1), from) : db.samples();
        double ms = 0;
        for (uint32_t i = from; i < to && i < db.samples(); ++i) { ms += db.sample(i).dtMs; if (db.sample(i).end) break; }
        c.Add(WeaponGroupOf(f), FoeArmourOf(f + kFoeArmor), float(ms / 1000));
    }
    return c;
}

// One stage: the player's weapon group and the foe it asks for, for so long.
struct Stage { uint8_t group = kFirstDrillGroup; FoeArmour armour = FoeArmour::Bare; uint8_t tier = 0; float seconds = 0; };
// Tour: every weapon against every kind of foe, in turn, as far as the time goes. Gaps: the pairs the recording has least of.
enum class DrillMode : uint8_t { Tour, Gaps };
constexpr float kStageSeconds = 75.f;                       // long enough for a few foes, short enough to see many pairs
constexpr int kMinDrillMinutes = 5, kMaxDrillMinutes = 60;

// The stages of a session of `minutes`. The same seed gives the same session. No weapon follows itself where another can.
inline std::vector<Stage> PlanSession(const Coverage& have, int minutes, DrillMode mode, uint32_t seed) {
    minutes = std::clamp(minutes, kMinDrillMinutes, kMaxDrillMinutes);
    const int count = std::max(1, int(float(minutes) * 60.f / kStageSeconds + .5f));
    struct Cell { uint8_t group; FoeArmour armour; float had; uint32_t tie; };
    std::mt19937 rng(seed);
    std::vector<Cell> cells;
    for (uint8_t g = kFirstDrillGroup; g <= kLastDrillGroup; ++g)
        for (int a = 0; a < kFoeArmours; ++a) cells.push_back({g, FoeArmour(a), have.at(g, FoeArmour(a)), uint32_t(rng())});
    // Tour: in a seeded order, each pair once before any twice. Gaps: least had first, and a pair that gets a stage counts as
    // having had it, so the time spreads over the thin ones rather than piling on one.
    std::vector<Stage> out;
    std::vector<float> had(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) had[i] = mode == DrillMode::Gaps ? cells[i].had : 0.f;
    std::vector<int> used(cells.size(), 0);
    for (int n = 0; n < count; ++n) {
        const uint8_t last = out.empty() ? 0xFF : out.back().group;
        size_t best = cells.size();
        for (size_t i = 0; i < cells.size(); ++i) {
            if (best == cells.size()) { best = i; continue; }
            auto key = [&](size_t k) { return std::tuple(cells[k].group == last, mode == DrillMode::Tour ? float(used[k]) : had[k], cells[k].tie); };
            if (key(i) < key(best)) best = i;
        }
        ++used[best]; had[best] += kStageSeconds;
        cells[best].tie = uint32_t(rng());   // its next turn falls elsewhere in the order
        out.push_back({cells[best].group, cells[best].armour, TierFor(cells[best].armour), kStageSeconds});
    }
    const float left = float(minutes) * 60.f - kStageSeconds * float(count);   // the rounding goes on the last stage
    out.back().seconds = std::max(30.f, out.back().seconds + left);
    return out;
}

// A Stage event's a and b: the group, and the tier and armour asked for.
inline FightEvent StageEvent(uint8_t index, const Stage& s) {
    return MakeEvent(Ev::Stage, kMe, kNoHand, index, float(s.group), float(s.tier | uint32_t(s.armour) << 4));
}
inline FightEvent SessionEndEvent() { return MakeEvent(Ev::Stage, kMe, kNoHand, kNoPart); }
inline Stage StageOf(const FightEvent& e) {
    Stage s;
    const uint32_t b = std::isfinite(e.b) && e.b >= 0 && e.b < 256 ? uint32_t(e.b) : 0;
    s.group = std::isfinite(e.a) && e.a >= 0 && e.a < kWeaponGroups ? uint8_t(e.a) : 0;
    s.tier = uint8_t(std::min(b & 15u, 7u)); s.armour = FoeArmour(b >> 4 & 3u);
    return s;
}

// The countdown and what comes next, for the banner: "Stage 3 of 16: two-handed sword vs mail, 0:42 left".
inline std::string StageBanner(size_t index, size_t count, const Stage& s, float secondsLeft) {
    const int left = std::max(0, int(secondsLeft + .999f));
    char t[16];
    std::snprintf(t, sizeof t, "%d:%02d", left / 60, left % 60);
    return "Stage " + std::to_string(index + 1) + " of " + std::to_string(count) + ": " + GroupName(s.group) + " vs " +
           FoeArmourName(s.armour) + ", " + t + " left";
}

// The clone's weapon (Fetch.ini clone_weapon=): kit, what the recording holds (as before pre11); random, a weapon it has moves for,
// new each time it comes in; match, the player's own weapon type when it has moves for it; or one weapon type. 3 + group - 1 for
// a group.
enum : uint8_t { kCloneKit = 0, kCloneRandom = 1, kCloneMatch = 2, kCloneGroup = 3 };
constexpr uint8_t kCloneWeaponChoices = kCloneGroup + kDrillGroups;
constexpr float kCloneGroupSeconds = 60.f;   // a weapon type the clone has less play with than this is not one it picks itself
inline const char* CloneWeaponToken(uint8_t v) {
    static const char* const n[] = {"kit", "random", "match", "sword1h", "sword2h", "mace1h", "mace2h", "axe1h", "axe2h", "polearm"};
    return v < kCloneWeaponChoices ? n[v] : "kit";
}
// A token, or a group's number 1..7. 0xFF: not one (the caller says so and keeps kit).
inline uint8_t ParseCloneWeapon(const std::string& value) {
    const std::string v = Lower(value);
    std::string t;
    for (char c : v) if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '-' && c != '_') t.push_back(c);
    if (t.empty()) return kCloneKit;
    if (t.size() == 1 && t[0] >= '1' && t[0] <= '7') return uint8_t(kCloneGroup + (t[0] - '1'));
    for (uint8_t i = 0; i < kCloneWeaponChoices; ++i) if (t == CloneWeaponToken(i)) return i;
    return 0xFF;
}
inline std::string CloneWeaponLabel(uint8_t v) {
    if (v == kCloneRandom) return "random";
    if (v == kCloneMatch) return "the same as mine";
    if (v >= kCloneGroup && v < kCloneWeaponChoices) return GroupName(uint8_t(v - kCloneGroup + kFirstDrillGroup));
    return "its own kit";
}
// Seconds of play per weapon group in a recording (what a clone of it has moves for).
inline std::array<float, kWeaponGroups> GroupSeconds(const Db& db) {
    std::array<float, kWeaponGroups> out{};
    for (uint32_t r = 0; r < db.rows(); ++r) {
        const uint32_t from = db.start(r), to = r + 1 < db.rows() ? std::max(db.start(r + 1), from) : db.samples();
        double ms = 0;
        for (uint32_t i = from; i < to && i < db.samples(); ++i) { ms += db.sample(i).dtMs; if (db.sample(i).end) break; }
        const uint8_t g = WeaponGroupOf(db.row(r));
        if (g < kWeaponGroups) out[g] += float(ms / 1000);
    }
    return out;
}
// The weapon group the clone is given as it comes in; 0 keeps its kit. had: bit g set when it has moves enough for group g.
// roll: any number (the same gives the same).
inline uint8_t PickCloneGroup(uint8_t setting, uint8_t playerGroup, uint32_t had, uint32_t roll) {
    if (setting >= kCloneGroup && setting < kCloneWeaponChoices) return uint8_t(setting - kCloneGroup + kFirstDrillGroup);
    if (setting == kCloneMatch)
        return playerGroup >= kFirstDrillGroup && playerGroup <= kLastDrillGroup && (had >> playerGroup & 1u) ? playerGroup : 0;
    if (setting != kCloneRandom) return 0;
    uint8_t pool[kDrillGroups];
    int n = 0;
    for (uint8_t g = kFirstDrillGroup; g <= kLastDrillGroup; ++g) if (had >> g & 1u) pool[n++] = g;
    return n ? pool[roll % uint32_t(n)] : 0;
}

}  // namespace hsm::clone
