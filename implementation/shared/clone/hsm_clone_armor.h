// What a fighter wears, as the other fighter can see it: which parts are bare, under cloth, mail or plate, and what kind of
// helmet is on (open, half, visored). A player sees this, not the game's protection numbers, and neither does the clone. And
// where a blow went: on bare skin, through a gap in the armour, through the armour, or stopped by it (StrikeClass). Pure
// logic: client/Fetch reads the pieces from the game (Adapter::ReadArmour), docs/CLONE.md.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hsm::clone {

enum class Mat : uint8_t { Bare, Cloth, Mail, Plate };
// Soft: a hat or cap of cloth. Open: a metal helmet with the face out (kettle hat, eisenhut). Half: one that hides more of the
// face but leaves it open (an open sallet, a barbute). Visored: the face behind metal, seen through a slit (a visor, an armet).
enum class Helm : uint8_t { None, Soft, Open, Half, Visored };
enum ArmourPart : uint8_t { kAHead, kAFace, kANeck, kAUpper, kALower, kAArmR, kAArmL, kALegR, kALegL, kArmourParts };
constexpr uint8_t kArmourSlots = 17;   // ArmorSlots_Enum

// A piece the game put on a fighter: its class name (BP_Armor_..._C), its slot, whether the game calls it metal, whether it has
// a visor that opens (its Visor Max Open is set), whether it hides the jaw, and its hit sound's name. The last three are the
// piece's own data, so a modded piece with a name this does not know still reads right.
struct ArmourPiece { std::string cls; uint8_t slot = 0xFF; bool metal = false, visor = false, hideJaw = false; std::string sound; };

inline std::string Lower(const std::string& s) {
    std::string o;
    for (char c : s) o.push_back(char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    return o;
}

// The parts a slot's piece covers, a bit per ArmourPart. The face is never a slot's: it is the helmet's (ViewOf). Gauntlets and
// shoes cover the hands and feet only, which a blow seldom finds, so they leave the arm and the leg as they were.
inline uint16_t SlotCovers(uint8_t slot) {
    constexpr uint16_t H = 1 << kAHead, N = 1 << kANeck, U = 1 << kAUpper, L = 1 << kALower, A = (1 << kAArmR) | (1 << kAArmL),
                       G = (1 << kALegR) | (1 << kALegL);
    //  0 helmet, 1 gauntlets, 2 main head, 3 neck, 4 bevor, 5 standard, 6 vambraces, 7 pauldrons, 8 outerwear, 9 cuirass,
    // 10 hauberk, 11 cuisses, 12 doublet, 13 foulds, 14 greaves, 15 shoes, 16 hosen
    constexpr uint16_t kCovers[kArmourSlots] = {H, 0, H, N, N, uint16_t(N | U), A, uint16_t(U | A), uint16_t(U | L), uint16_t(U | L),
                                                uint16_t(U | L | A), G, uint16_t(U | L | A), L, G, 0, uint16_t(G | L)};
    return slot < kArmourSlots ? kCovers[slot] : 0;
}

// Mail: the game's metal flag does not tell mail from plate. The hit sound does (the game tells metal from cloth by its name the
// same way), and so do the game's own names.
inline Mat PieceMaterial(const ArmourPiece& p) {
    const std::string c = Lower(p.cls), snd = Lower(p.sound);
    for (const char* k : {"mail", "chain"}) if (snd.find(k) != std::string::npos) return Mat::Mail;
    for (const char* k : {"chain", "hauberk", "mail", "standart"}) if (c.find(k) != std::string::npos) return Mat::Mail;
    return p.metal || snd.find("metal") != std::string::npos ? Mat::Plate : Mat::Cloth;
}

inline bool HeadSlot(const ArmourPiece& p) { return p.slot == 0 || p.slot == 2 || Lower(p.cls).find("_head_") != std::string::npos; }

// The helmet kind of one of the game's own pieces by its name; false when the name is not one this knows.
inline bool HelmByName(const ArmourPiece& p, Helm& h) {
    const std::string c = Lower(p.cls);
    auto has = [&](const char* k) { return c.find(k) != std::string::npos; };
    if (has("visor") || has("armet") || has("sallet_solid")) { h = Helm::Visored; return true; }   // a visor module too
    if (!HeadSlot(p)) return false;
    if (has("sallet") || has("barbute")) { h = Helm::Half; return true; }
    if (has("kettle") || has("eisenhut")) { h = Helm::Open; return true; }
    if (has("hat") || has("bycocket") || has("gnome") || has("cap")) { h = p.metal ? Helm::Open : Helm::Soft; return true; }
    return false;
}

// What kind of helmet a piece is, or None when it is not one. A name the game uses decides; any other piece (a modded one) by its
// data: a visor that opens, or a metal helmet that hides the jaw, has the face behind metal.
inline Helm HelmOf(const ArmourPiece& p) {
    Helm h;
    if (HelmByName(p, h)) return h;
    if (p.visor) return Helm::Visored;
    if (!HeadSlot(p)) return Helm::None;
    if (PieceMaterial(p) == Mat::Cloth) return Helm::Soft;
    return p.hideJaw ? Helm::Visored : Helm::Open;
}

struct ArmourView {
    std::array<Mat, kArmourParts> part{};
    Helm helm = Helm::None;
    bool operator==(const ArmourView&) const = default;
    bool any() const { return helm != Helm::None || std::any_of(part.begin(), part.end(), [](Mat m) { return m != Mat::Bare; }); }
    // 2 bits a part, the helmet above: 21 bits, a whole number a float holds exactly (an Armour event's a).
    uint32_t Pack() const {
        uint32_t v = uint32_t(helm) << (2 * kArmourParts);
        for (int i = 0; i < kArmourParts; ++i) v |= uint32_t(part[size_t(i)]) << (2 * i);
        return v;
    }
    static ArmourView Unpack(uint32_t v) {
        ArmourView o;
        for (int i = 0; i < kArmourParts; ++i) o.part[size_t(i)] = Mat(v >> (2 * i) & 3u);
        o.helm = Helm(std::min<uint32_t>(v >> (2 * kArmourParts) & 7u, uint32_t(Helm::Visored)));
        return o;
    }
};

// A feature row's armour columns (kParts order: head, upper, lower, arm R, arm L, leg R, leg L): 0 bare, .5 cloth, .75 mail,
// 1 plate. The head is the helmet, or the face behind a visor.
constexpr float kSoftArmor = .5f, kMailArmor = .75f;
inline float MatCover(Mat m) { return m == Mat::Plate ? 1.f : m == Mat::Mail ? kMailArmor : m == Mat::Cloth ? kSoftArmor : 0.f; }
inline void Cover7(const ArmourView& v, float* c) {
    c[0] = std::max(MatCover(v.part[kAHead]), MatCover(v.part[kAFace]));
    const ArmourPart from[6] = {kAUpper, kALower, kAArmR, kAArmL, kALegR, kALegL};
    for (int i = 0; i < 6; ++i) c[i + 1] = MatCover(v.part[from[i]]);
}

inline ArmourView ViewOf(const std::vector<ArmourPiece>& pieces) {
    ArmourView v;
    bool bevor = false;
    for (const ArmourPiece& p : pieces) {
        const Mat m = PieceMaterial(p);
        const uint16_t covers = SlotCovers(p.slot);
        for (int i = 0; i < kArmourParts; ++i) if (covers >> i & 1u) v.part[size_t(i)] = std::max(v.part[size_t(i)], m);
        const Helm h = HelmOf(p);
        if (h != Helm::None) { v.helm = std::max(v.helm, h); v.part[kAHead] = std::max(v.part[kAHead], m); }
        if ((p.slot == 4 || (p.slot == 3 && p.hideJaw)) && m != Mat::Cloth) bevor = true;   // a bevor, or a neck piece over the jaw
    }
    // The face: behind a visor, or behind a bevor under a metal helmet (a sallet and bevor), seen through a slit either way.
    if (v.helm == Helm::Visored || (bevor && v.helm >= Helm::Open)) v.part[kAFace] = Mat::Plate;
    return v;
}

inline const char* MatName(Mat m) { static const char* const n[] = {"bare", "cloth", "mail", "plate"}; return n[size_t(m) & 3u]; }
inline const char* HelmName(Helm h) { static const char* const n[] = {"none", "soft", "open", "half", "visored"}; return n[std::min<size_t>(size_t(h), 4)]; }
inline const char* ArmourPartName(uint8_t p) {
    static const char* const n[] = {"head", "face", "neck", "upper", "lower", "armR", "armL", "legR", "legL"};
    return p < kArmourParts ? n[p] : "?";
}
// "helm=visored head=plate face=plate neck=mail upper=plate ...", for the log.
inline std::string Describe(const ArmourView& v) {
    std::string o = std::string("helm=") + HelmName(v.helm);
    for (uint8_t i = 0; i < kArmourParts; ++i) { o += ' '; o += ArmourPartName(i); o += '='; o += MatName(v.part[i]); }
    return o;
}

// Where on the body a bone of the fighter's skeleton is, by the start of its name (fingers and twist bones go with their limb).
// The head bone is the head: whether a blow on it found the face is the helmet's to say (ClassifyStrike).
inline uint8_t ArmourPartOfBone(const std::string& bone) {
    const std::string b = Lower(bone);
    auto starts = [&](const char* p) { return b.rfind(p, 0) == 0; };
    auto side = [&](uint8_t right, uint8_t left) -> uint8_t {
        const size_t n = b.size();
        if ((n >= 2 && b[n - 2] == '_' && b[n - 1] == 'r') || b.find("_r_") != std::string::npos) return right;
        if ((n >= 2 && b[n - 2] == '_' && b[n - 1] == 'l') || b.find("_l_") != std::string::npos) return left;
        return 0xFF;
    };
    if (starts("head")) return kAHead;
    if (starts("neck")) return kANeck;
    if (starts("spine_03") || starts("spine_04") || starts("spine_05") || starts("clavicle")) return kAUpper;
    if (starts("pelvis") || starts("spine_01") || starts("spine_02") || starts("root")) return kALower;
    if (starts("upperarm") || starts("lowerarm") || starts("hand") || starts("thumb") || starts("index") || starts("middle") ||
        starts("ring") || starts("pinky")) return side(kAArmR, kAArmL);
    if (starts("thigh") || starts("calf") || starts("foot") || starts("ball")) return side(kALegR, kALegL);
    return 0xFF;
}

// How a blow went. Bare: it found skin where nothing was worn (or only cloth). Gap: it found skin on a part under mail or plate,
// so it went between the pieces (or through a visor's slit). Beat: it hit the armour and still hurt. Held: the armour stopped it.
enum class StrikeClass : uint8_t { Bare, Gap, Beat, Held };
constexpr uint8_t kSurfaceFlesh = 1;    // the game's surface type of skin (Deal Complex Damage's Hit Surface)
constexpr float kBeatDamage = 2.f;      // RUNTIME: below this a blow on armour is taken as stopped; the strike log shows real values
inline StrikeClass ClassifyStrike(uint8_t surface, float damage, float cutRate, uint8_t part, const ArmourView& v) {
    if (surface == kSurfaceFlesh) {
        if (part == kAHead) return v.part[kAFace] >= Mat::Mail ? StrikeClass::Gap : StrikeClass::Bare;   // a blow to the head on skin is in the face
        return part < kArmourParts && v.part[part] >= Mat::Mail ? StrikeClass::Gap : StrikeClass::Bare;
    }
    return damage >= kBeatDamage || cutRate > 0 ? StrikeClass::Beat : StrikeClass::Held;
}
inline const char* StrikeClassName(StrikeClass c) { static const char* const n[] = {"bare", "gap", "beat", "held"}; return n[size_t(c) & 3u]; }

}  // namespace hsm::clone
