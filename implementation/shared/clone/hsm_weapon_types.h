#pragma once
// The game's own weapon types: Enum_WeaponType_Specific, in its order. Forged weapons belong to one by what they are made of, and
// the classes below by the hand list and the shape the game gives them (it has no finer type for those). Plain data, no engine.
#include <cstdint>
#include <cstring>
#include <string>

#include "sync/hsm_pose_codec.h"

namespace hsm::clone {

enum class WeaponType : uint8_t { Unknown = 0, Swords1H, Swords2H, Maces1H, Maces2H, Hafted1H, Hafted2H, Polearms };
constexpr int kWeaponTypes = 8;

struct WeaponClassType { const char* name; WeaponType type; };
// tools/hsmclone.py reads this list. A class not here is forged, a shield, a thing to throw, or one the game added since: 0.
constexpr WeaponClassType kWeaponClasses[] = {
    {"BP_Weapon_Tool_Axe_C_C", WeaponType::Hafted1H},
    {"BP_Weapon_Tool_Axe_D_C", WeaponType::Hafted1H},
    {"BP_Weapon_Tool_Hoe_B_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_1_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_2_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_3_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_4_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_5_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_6_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_Hulbat_A_002_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_Hulbat_B_001_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_Hulbat_B_002_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_Hulbat_C_002_C", WeaponType::Hafted1H},
    {"ModularWeaponBP_Axe_Hulbat_D_001_C", WeaponType::Hafted1H},
    {"BP_Weapon_Tool_Axe_A_C", WeaponType::Hafted2H},
    {"BP_Weapon_Tool_Axe_B_C", WeaponType::Hafted2H},
    {"BP_Weapon_Tool_Hoe_A_C", WeaponType::Hafted2H},
    {"BP_Weapon_Tool_Pickaxe_A_C", WeaponType::Hafted2H},
    {"BP_Weapon_Tool_Pickaxe_B_C", WeaponType::Hafted2H},
    {"ModularWeaponBP_Axe2H_C", WeaponType::Hafted2H},
    {"BP_Weapon_Tool_Hammer_A_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Hammer_B_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Hammer_C_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Mallet_A_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Mallet_B_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Mallet_C_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Tongs_C", WeaponType::Maces1H},
    {"BP_Weapon_Tool_Maul_C", WeaponType::Maces2H},
    {"ModularWeaponBP_Flail_A_C", WeaponType::Maces2H},
    {"ModularWeaponBP_Flail_B_C", WeaponType::Maces2H},
    {"ModularWeaponBP_Flail_C_C", WeaponType::Maces2H},
    {"ModularWeaponBP_Flail_D_C", WeaponType::Maces2H},
    {"ModularWeaponBP_Tool_Flail_C", WeaponType::Maces2H},
    {"BP_Weapon_Tool_Pitchfork_A_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Pitchfork_B_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Rake_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Scythe_A_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Shovel_A_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Shovel_B_C", WeaponType::Polearms},
    {"BP_Weapon_Tool_Sickle_E_C", WeaponType::Polearms},
    {"ModularWeaponBP_BaronBeak_C", WeaponType::Polearms},
    {"ModularWeaponBP_Billhook_A_C", WeaponType::Polearms},
    {"ModularWeaponBP_Billhook_B_C", WeaponType::Polearms},
    {"ModularWeaponBP_Billhook_C_C", WeaponType::Polearms},
    {"ModularWeaponBP_Halberd_B_C", WeaponType::Polearms},
    {"ModularWeaponBP_Halberd_C_C", WeaponType::Polearms},
    {"ModularWeaponBP_Halberd_D_C", WeaponType::Polearms},
    {"ModularWeaponBP_Spear_A_C", WeaponType::Polearms},
    {"ModularWeaponBP_Spear_B_C", WeaponType::Polearms},
    {"ModularWeaponBP_Spear_C_C", WeaponType::Polearms},
    {"ModularWeaponBP_Staff_C", WeaponType::Polearms},
    {"ModularWeaponBP_WarStaff_A_C", WeaponType::Polearms},
    {"ModularWeaponBP_WarStaff_B_C", WeaponType::Polearms},
    {"BP_Weapon_Reforged_Baurnwehr_A_002_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_A_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_B_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_C_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_D_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_E_C", WeaponType::Swords1H},
    {"BP_Weapon_Reforged_Baurnwehr_F_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Chisel_B_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Chisel_C_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Knife_A_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Knife_B_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Knife_C_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Scissors_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Sickle_A_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Sickle_B_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Sickle_C_C", WeaponType::Swords1H},
    {"BP_Weapon_Tool_Sickle_D_C", WeaponType::Swords1H},
    {"ModularWeaponBP_DaggerRondel_C", WeaponType::Swords1H},
};

// A held weapon's class, as a recording keeps it.
inline WeaponType WeaponTypeOfClass(uint32_t classHash) {
    for (const WeaponClassType& c : kWeaponClasses) if (sync::WeaponClassHash(c.name) == classHash) return c.type;
    return WeaponType::Unknown;
}

namespace detail {
inline std::string Lower(const char* s) { std::string o(s ? s : ""); for (char& c : o) if (c >= 'A' && c <= 'Z') c = char(c + 32); return o; }
inline bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
inline bool Ends(const std::string& s, const char* tail) { const size_t n = std::strlen(tail); return s.size() >= n && s.compare(s.size() - n, n, tail) == 0; }
}

// A forged weapon: every one is the same class, what it is lies in its Module Head and Grip Module (BP_GameWeapon_Customizable_*
// list them). The blades are shared between one and two handed swords and the mace heads between short and long, so the head says
// the family and the grip the hands: sword grips are numbered by length, 15 being the longsword's; maces end _S, _M, _L and cast
// ones A, B, C; hafts end _Short, _Avg, _Long. Unknown: not a part the game had when this was written.
inline WeaponType ForgedWeaponType(const char* headClass, const char* gripClass) {
    using detail::Has; using detail::Ends;
    std::string head = detail::Lower(headClass), grip = detail::Lower(gripClass);
    if (Ends(head, "_c")) head.resize(head.size() - 2);
    if (Ends(grip, "_c")) grip.resize(grip.size() - 2);
    if (head.empty() || grip.empty()) return WeaponType::Unknown;
    if (Has(head, "polearm") || Has(head, "_pa_")) return WeaponType::Polearms;
    if (Has(head, "mace")) return Ends(grip, "_l") || (Has(grip, "casted_grip_") && Ends(grip, "c")) ? WeaponType::Maces2H : WeaponType::Maces1H;
    if (Has(head, "hafted")) return Ends(grip, "_long") ? WeaponType::Hafted2H : WeaponType::Hafted1H;
    if (Has(head, "blade")) return Has(grip, "grip_15") ? WeaponType::Swords2H : WeaponType::Swords1H;
    return WeaponType::Unknown;
}

}  // namespace hsm::clone
