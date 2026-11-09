// A trained style clone. tools/clonetrain turns a Clones/<name>.hsmclone into Clones/<name>.hsmmodel: a few MB however long
// the play, holding small nets, a bank of the player's real clips, their name and their kit. A few times a second the nets
// say which kinds of move the player would make now and what they would change; ModelBrain plays a real clip of that move,
// so a swing stays a swing and never an average of two. Plain C++, no runtime. A file comes from someone else: Model::Load
// checks all of it and refuses the whole file on any fault. docs/CLONE.md, training/data/split/RULE.md.
#pragma once
#include "clone/hsm_clone.h"
#include <bit>
#include <string_view>

namespace hsm::clone {

// 4: the nets also hear the fight's events (EventSense). A model of 3 still loads, and plays without them.
// 5: the nets also hear what both fighters wear (the armour inputs, which recordings before Fetch pre11 had wrong). The same
// layout as 4: a model of 4 still loads, and its nets scale the armour inputs to nothing, as they were trained.
constexpr uint32_t kModelVersion = 5, kModelVersionNoArmour = 4, kModelVersionNoEvents = 3;
constexpr int kHistory = 11, kSummary = 9;   // summary: bits W S A D guardR guardL, turn/60, look/20, run
constexpr int kNetInNoEvents = kFeatures + kHistory, kNetIn = kNetInNoEvents + kEventInputs;   // features, InputHistory, EventSense
constexpr uint32_t kMaxNets = 4, kMaxHidden = 1024, kMaxTokens = 4096, kMaxVariants = 64, kMaxClipSamples = 64;
constexpr size_t kMaxModelFile = size_t(64) << 20;
// The decoder, chosen on validation recordings only.
constexpr float kModelDecideMs = 200.f;   // a new clip this often
constexpr float kModelTemp = 1.15f;       // noise between tokens: much less and the clone freezes, much more and it twitches
constexpr float kModelTheta = .33f, kModelThetaGuard = .3f;   // the head's chance of a change for it to count
constexpr float kModelBits = 2.f;         // cost of a clip not doing the change asked for
constexpr float kModelHeld = 4.f;         // cost of each held input a clip did not start from
constexpr float kModelMouse = 2.f;        // cost of a clip's mouse not going on from the clone's
constexpr float kModelFloor = -10.f;      // tokens this far under the best are never played
constexpr float kModelInputClamp = 16.f;  // scaled inputs stop here
constexpr float kModelOutClamp = 1e4f;
constexpr float kModelMaxNorm = 1e6f;

// Half floats: all a weight or a mouse delta needs, and half the file.
inline float FromHalf(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16, exp = h >> 10 & 31u, man = h & 1023u;
    float f;
    if (exp == 0) { f = float(man) * 5.9604644775390625e-8f; return sign ? -f : f; }
    const uint32_t x = sign | (exp == 31 ? 0x7f800000u | man << 13 : (exp + 112) << 23 | man << 13);
    std::memcpy(&f, &x, 4);
    return f;
}
inline uint16_t ToHalf(float v) {
    uint32_t x; std::memcpy(&x, &v, 4);
    const uint16_t sign = uint16_t(x >> 16 & 0x8000u);
    const uint32_t a = x & 0x7fffffffu;
    if (a > 0x7f800000u) return uint16_t(sign | 0x7e00u);
    if (a >= 0x47800000u) return uint16_t(sign | 0x7c00u);
    if (a < 0x38800000u) { float f; std::memcpy(&f, &a, 4); return uint16_t(sign | uint16_t(std::nearbyint(f * 16777216.f))); }
    const uint32_t r = a - 0x38000000u;
    return uint16_t(sign | uint16_t((r + 0xfffu + (r >> 13 & 1u)) >> 13));
}

// What the 42 features leave out: how long each input has been held, how busy the mouse has been.
class InputHistory {
public:
    InputHistory() { Reset(); }
    void Reset() { ring_.clear(); for (int i = 0; i < 7; ++i) { since_[i] = kLongAgo; on_[i] = false; } }
    void Add(float dtMs, float turn, float look, uint32_t buttons, float guardR, float guardL) {
        const bool now[7] = {bool(buttons >> kKeyBit & 1u), bool(buttons >> (kKeyBit + 1) & 1u), bool(buttons >> (kKeyBit + 2) & 1u), bool(buttons >> (kKeyBit + 3) & 1u), guardR > .5f, guardL > .5f, bool(buttons >> kRunBit & 1u)};
        for (int i = 0; i < 7; ++i) { since_[i] = now[i] != on_[i] ? 0 : std::min(since_[i] + dtMs, kLongAgo); on_[i] = now[i]; }
        ring_.push_back({dtMs, turn, look});
        float held = 0;
        for (const Moved& m : ring_) held += m.ms;
        while (held > 1000 && ring_.size() > 1) { held -= ring_.front().ms; ring_.pop_front(); }
    }
    void Fill(float* o) const {
        for (int i = 0; i < 7; ++i) o[i] = std::log1p(since_[i] / 100.f) / 3.7f;
        float e5 = 0, e10 = 0, t = 0, l = 0, age = 0;
        for (auto it = ring_.rbegin(); it != ring_.rend(); ++it) {
            const float a = std::fabs(it->turn) + std::fabs(it->look);
            e10 += a;
            if (age < 500) e5 += a;
            if (age >= 150 && age < 450) { t += it->turn; l += it->look; }
            age += it->ms;
        }
        o[7] = std::log1p(e5 / 8.f) / 4; o[8] = std::log1p(e10 / 8.f) / 4; o[9] = std::tanh(t / 60.f); o[10] = std::tanh(l / 30.f);
    }
private:
    static constexpr float kLongAgo = 4000.f;
    struct Moved { float ms, turn, look; };
    std::deque<Moved> ring_;
    float since_[7];
    bool on_[7];
};

// ---------------------------------------------------------------- the file
enum class ModelBlock : uint8_t { Meta = 1, Norm = 2, Net = 3, Bank = 4, Name = 5, Kit = 6 };
// What goes in. The trainer fills this, EncodeModel writes it.
struct NetParts { std::vector<float> w1, b1, w2, b2, w3, b3; };   // w: rows of inputs, each as wide as the layer's output
struct ClipParts {
    float bit[7] = {};              // share of the next 300 ms each input was held: W S A D guardR guardL run
    float turn = 0, look = 0;       // mouse over those 300 ms
    float prevTurn = 0, prevLook = 0, foeX = 0, foeY = 0;   // features 33, 34, 17, 18 where it started
    uint8_t held = 0;               // what was held where it started, as bit[]
    uint8_t group = 0;              // the weapon in hand where it started, WeaponGroup
    std::vector<Sample> samples;    // none: an empty slot
    uint32_t source = 0;            // the clone file sample it started at; for the lab, not in the file
};
struct ModelParts {
    std::string name; std::vector<uint8_t> kit;
    uint32_t hidden = 0, tokens = 0, variants = 0; float playedSeconds = 0;
    std::array<float, kNetIn> mean{}, inv{};
    std::vector<NetParts> nets;
    std::vector<ClipParts> clips;   // tokens * variants, by token
};
inline std::vector<uint8_t> ModelHeader(uint32_t version = kModelVersion) {
    std::vector<uint8_t> o;
    PutBytes(o, "HSMMODEL", 8); PutU32(o, version); PutU32(o, uint32_t(version == kModelVersionNoEvents ? kNetInNoEvents : kNetIn));
    return o;
}
inline std::vector<uint8_t> EncodeModelBlock(ModelBlock kind, const uint8_t* p, size_t n) { return EncodeBlock(Block(kind), p, n); }
namespace detail {
inline void PutHalf(std::vector<uint8_t>& o, float v) { const uint16_t h = ToHalf(v); o.push_back(uint8_t(h)); o.push_back(uint8_t(h >> 8)); }
inline void PutF32(std::vector<uint8_t>& o, float v) { PutBytes(o, &v, 4); }
inline float Half(const uint8_t* p) { return FromHalf(uint16_t(p[0] | p[1] << 8)); }
}  // namespace detail
inline std::vector<uint8_t> EncodeModel(const ModelParts& m) {
    std::vector<uint8_t> out = ModelHeader(), b;
    auto put = [&](ModelBlock k) { const auto e = EncodeModelBlock(k, b.data(), b.size()); out.insert(out.end(), e.begin(), e.end()); b.clear(); };
    PutU32(b, uint32_t(m.nets.size())); PutU32(b, m.hidden); PutU32(b, m.tokens); PutU32(b, m.variants); detail::PutF32(b, m.playedSeconds);
    put(ModelBlock::Meta);
    for (float v : m.mean) detail::PutF32(b, v);
    for (float v : m.inv) detail::PutF32(b, v);
    put(ModelBlock::Norm);
    for (const NetParts& n : m.nets) {
        for (const auto* v : {&n.w1, &n.b1, &n.w2, &n.b2, &n.w3, &n.b3}) for (float x : *v) detail::PutHalf(b, x);
        put(ModelBlock::Net);
    }
    uint16_t axes = 0;
    for (const ClipParts& c : m.clips) for (const Sample& s : c.samples) for (size_t a = 0; a < kAxisCount; ++a) if (s.axes[a] != 0) axes = uint16_t(axes | 1u << a);
    b.push_back(uint8_t(axes)); b.push_back(uint8_t(axes >> 8));
    for (const ClipParts& c : m.clips) {
        b.push_back(uint8_t(c.samples.size()));
        if (c.samples.empty()) continue;
        for (float v : c.bit) b.push_back(uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + .5f));
        b.push_back(c.held); b.push_back(c.group);
        for (float v : {c.turn, c.look, c.prevTurn, c.prevLook, c.foeX, c.foeY}) detail::PutHalf(b, v);
        for (const Sample& s : c.samples) {
            detail::PutHalf(b, std::min(s.dtMs, 60000.f));
            for (int i = 0; i < 3; ++i) b.push_back(uint8_t(s.buttons >> (8 * i)));
            detail::PutHalf(b, s.dyaw); detail::PutHalf(b, s.dpitch);
            for (size_t a = 0; a < kAxisCount; ++a) if (axes >> a & 1u) detail::PutHalf(b, std::clamp(s.axes[a], -60000.f, 60000.f));
        }
    }
    put(ModelBlock::Bank);
    b.assign(m.name.begin(), m.name.end()); put(ModelBlock::Name);
    b = m.kit; put(ModelBlock::Kit);
    return out;
}

struct ModelClip { float bit[7], turn, look, prevTurn, prevLook, bearing; uint8_t held, group; uint32_t first; uint32_t count; };
// A weapon nobody fought with plays the nearest there is: the same family first, then what swings alike; fists only ever come last.
constexpr uint8_t kGroupOrder[kWeaponGroups][kWeaponGroups] = {
    {0, 8, 9, 1, 3, 5, 2, 4, 6, 7},   // no weapon
    {1, 9, 8, 2, 3, 5, 4, 6, 7, 0},   // swords 1H
    {2, 9, 1, 8, 6, 4, 7, 3, 5, 0},   // swords 2H
    {3, 5, 8, 9, 1, 4, 6, 2, 7, 0},   // maces 1H
    {4, 6, 3, 5, 9, 2, 7, 8, 1, 0},   // maces 2H
    {5, 3, 8, 9, 1, 6, 4, 2, 7, 0},   // hafted 1H
    {6, 4, 5, 3, 9, 7, 2, 8, 1, 0},   // hafted 2H
    {7, 6, 4, 2, 9, 5, 3, 1, 8, 0},   // polearms
    {8, 9, 1, 3, 5, 2, 4, 6, 7, 0},   // no type, short
    {9, 8, 1, 2, 5, 3, 6, 4, 7, 0},   // no type, middling
};
// A group with under one move in this many also plays the nearest moves of no known type, which may well be the same weapon: most
// of a player's swords can be forged ones from before recordings kept their type. Another known type is not, so it only stands in
// for a type never played; on validation, filling up from one cost mouse correlation (.445 to .41).
constexpr uint32_t kThinGroup = 16;
// The game reads a grab by when it is let go: under .25 s the hand drops what it holds, over that it sheathes it
// (Willie_BP InpActEvt_Grab_*). So in a hand that holds something only a press seen whole in the clip goes through: that one the
// player meant. The tail of a press from before the clip, or a head cut off by the next, would be a press of the wrong length.
inline uint64_t GrabMask(const Sample* s, uint32_t n, int action) {
    uint64_t hide = 0;
    for (uint32_t i = 0; i < n;) {
        if (!(s[i].buttons >> action & 1u)) { ++i; continue; }
        uint32_t j = i;
        while (j < n && s[j].buttons >> action & 1u) ++j;
        if (i == 0 || j == n) for (uint32_t k = i; k < j; ++k) hide |= uint64_t(1) << k;
        i = j;
    }
    return hide;
}
class ModelRng {   // small and the same everywhere
public:
    explicit ModelRng(uint32_t seed) : s_(seed * 2654435761u + 1u) {}
    float Gumbel() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; const float u = (float(s_ >> 8) + .5f) / 16777216.f; return -std::log(-std::log(u)); }
private:
    uint32_t s_;
};
class Model;
// Room for one decision, so a decision never allocates.
struct ModelScratch { std::vector<float> z, a, b, y, sum; explicit ModelScratch(const Model& m); };

class Model {
public:
    // false: refused, all of it, and error() says why. A model never half loads.
    bool Load(const uint8_t* data, size_t size) {
        Clear();
        const char* why = Read(data, size);
        if (why) { Clear(); error_ = why; return false; }
        return true;
    }
    const char* error() const { return error_; }
    const std::string& name() const { return name_; }
    const std::vector<uint8_t>& kit() const { return kit_; }   // not checked here (CheckPassportSave)
    uint32_t nets() const { return uint32_t(nets_.size()); }
    uint32_t hidden() const { return hidden_; }
    uint32_t tokens() const { return tokens_; }
    uint32_t plays(int group) const { return plays_[size_t(group)]; }   // the groups a fighter of this group plays, a bit each
    uint32_t variants() const { return variants_; }
    float playedSeconds() const { return played_; }
    uint32_t clips() const { return uint32_t(clips_.size()); }
    const ModelClip& clip(uint32_t i) const { return clips_[i]; }
    const Sample& sample(uint32_t i) const { return samples_[i]; }
    int slot(uint32_t i) const { return slots_[i]; }   // a slot's clip, -1 empty
    uint32_t outputs() const { return tokens_ + uint32_t(kSummary); }
    uint32_t inputs() const { return netIn_; }   // kNetIn, or kNetInNoEvents for a model of version 3
    bool hearsEvents() const { return netIn_ == uint32_t(kNetIn); }

    // The nets on one view: y holds the tokens' logits, then the summary. events: EventSense::Fill; null, not known.
    void Forward(const float* seen, const float* hist, ModelScratch& s, const float* events = nullptr) const {
        float unknown[kEventInputs];
        if (!events) { EventSense(false).Fill(unknown); events = unknown; }
        for (uint32_t i = 0; i < netIn_; ++i) {
            const float v = i < uint32_t(kFeatures) ? seen[i] : i < uint32_t(kNetInNoEvents) ? hist[i - kFeatures] : events[i - kNetInNoEvents];
            const float z = (v - mean_[i]) * inv_[i];
            s.z[i] = std::isfinite(z) ? std::clamp(z, -kModelInputClamp, kModelInputClamp) : 0.f;
        }
        const uint32_t out = outputs();
        std::fill(s.sum.begin(), s.sum.end(), 0.f);
        for (const Net& n : nets_) {
            Layer(s.z.data(), netIn_, n.w1.data(), n.b1.data(), hidden_, true, s.a.data());
            Layer(s.a.data(), hidden_, n.w2.data(), n.b2.data(), hidden_, true, s.b.data());
            Layer(s.b.data(), hidden_, n.w3.data(), n.b3.data(), out, false, s.y.data());
            for (uint32_t j = 0; j < out; ++j) s.sum[j] += s.y[j];
        }
        const float inv = 1.f / float(nets_.size());
        for (uint32_t j = 0; j < out; ++j) { const float v = s.sum[j] * inv; s.y[j] = std::isfinite(v) ? std::clamp(v, -kModelOutClamp, kModelOutClamp) : 0.f; }
    }
    // Chooses a clip from the logits and summary Forward left. The head only asks for changes: where it asks for none, a clip
    // need only start from what is held. Noise is per token; within a token the clip is chosen for fit. -1: nothing to play.
    int Choose(const float* seen, ModelRng& g, const ModelScratch& sc) const {
        const float* y = sc.y.data(); const float* s = y + tokens_;
        const int group = WeaponGroupOf(seen);
        const uint32_t plays = plays_[size_t(group)];
        const std::vector<uint8_t>& has = moves_[size_t(group)];
        float top = -3.4e38f;
        for (uint32_t t = 0; t < tokens_; ++t) if (has[t]) top = std::max(top, y[t]);
        float cur[7], want[7];
        for (int k = 0; k < 7; ++k) cur[k] = seen[35 + k] > .5f ? 1.f : 0.f;
        for (int k = 0; k < 7; ++k) {
            const float p = k < 6 ? s[k] : s[8], th = k == 4 || k == 5 ? kModelThetaGuard : kModelTheta;
            want[k] = cur[k] > .5f ? (p < 1 - th ? 0.f : 1.f) : (p > th ? 1.f : 0.f);
        }
        uint32_t heldNow = 0, asked = 0;
        for (int k = 0; k < 7; ++k) { if (cur[k] > .5f) heldNow |= 1u << k; if (want[k] != cur[k]) asked |= 1u << k; }
        const float q33 = std::isfinite(seen[33]) ? seen[33] : 0.f, q34 = std::isfinite(seen[34]) ? seen[34] : 0.f;
        float best = -3.4e38f; int pick = -1;
        for (uint32_t t = 0; t < tokens_; ++t) {
            const float lp = y[t] - top;
            if (!has[t] || lp < kModelFloor) continue;
            const float base = lp + kModelTemp * g.Gumbel();
            if (base < best) continue;
            for (uint32_t j = 0; j < variants_; ++j) {
                const int ci = slots_[t * variants_ + j];
                if (ci < 0 || !(plays >> clips_[uint32_t(ci)].group & 1u)) continue;
                const ModelClip& c = clips_[uint32_t(ci)];
                float eb = 0;
                for (int k = 0; k < 7; ++k) if (asked >> k & 1u) { const float x = c.bit[k] - want[k]; eb += x * x; }
                const int miss = std::popcount((c.held ^ heldNow) & ~asked & 127u);
                const float a = c.prevTurn - q33, e = c.prevLook - q34;
                const float score = base - kModelBits * eb - kModelHeld * float(miss) - kModelMouse * (a * a + e * e);
                if (score > best) { best = score; pick = ci; }
            }
        }
        return pick;
    }
    int Pick(const float* seen, const float* hist, ModelRng& g, ModelScratch& s, const float* events = nullptr) const { Forward(seen, hist, s, events); return Choose(seen, g, s); }
private:
    struct Net { std::vector<float> w1, b1, w2, b2, w3, b3; };
    static void Layer(const float* in, uint32_t n, const float* w, const float* b, uint32_t out, bool relu, float* o) {
        std::copy(b, b + out, o);
        for (uint32_t i = 0; i < n; ++i) {
            const float x = in[i];
            if (x == 0) continue;
            const float* r = w + size_t(i) * out;
            for (uint32_t j = 0; j < out; ++j) o[j] += x * r[j];
        }
        if (relu) for (uint32_t j = 0; j < out; ++j) o[j] = std::max(o[j], 0.f);
    }
    void Clear() {
        error_ = nullptr; name_.clear(); kit_.clear(); nets_.clear(); clips_.clear(); samples_.clear(); slots_.clear();
        hidden_ = tokens_ = variants_ = wantNets_ = netIn_ = 0; played_ = 0; mean_.fill(0); inv_.fill(0);
    }
    const char* Read(const uint8_t* data, size_t size) {
        if (!data || size > kMaxModelFile) return "not a file of a size a model can be";
        const std::vector<uint8_t> head = ModelHeader(), noArmour = ModelHeader(kModelVersionNoArmour), old = ModelHeader(kModelVersionNoEvents);
        if (size >= head.size() && std::memcmp(data, head.data(), head.size()) == 0) netIn_ = uint32_t(kNetIn);
        else if (size >= noArmour.size() && std::memcmp(data, noArmour.data(), noArmour.size()) == 0) netIn_ = uint32_t(kNetIn);
        else if (size >= old.size() && std::memcmp(data, old.data(), old.size()) == 0) netIn_ = uint32_t(kNetInNoEvents);
        else return "not a model of this version";
        bool meta = false, norm = false, bank = false, name = false, kit = false;
        size_t p = head.size();
        while (p < size) {
            if (size - p < 9) return "cut short";
            uint32_t len, crc;
            const uint8_t kind = data[p];
            std::memcpy(&len, data + p + 1, 4);
            if (size - p - 9 < len) return "cut short";
            const uint8_t* d = data + p + 5;
            std::memcpy(&crc, d + len, 4);
            if (crc != util::Crc32(d, len)) return "bad checksum";
            if (!meta && kind != uint8_t(ModelBlock::Meta)) return "no shape first";
            const char* why = nullptr;
            switch (ModelBlock(kind)) {
                case ModelBlock::Meta: why = meta ? "two shapes" : ReadMeta(d, len); meta = true; break;
                case ModelBlock::Norm: why = norm ? "two scales" : ReadNorm(d, len); norm = true; break;
                case ModelBlock::Net: why = ReadNet(d, len); break;
                case ModelBlock::Bank: why = bank ? "two banks" : ReadBank(d, len); bank = true; break;
                case ModelBlock::Name: if (name || len > kMaxName * 4) why = "bad name"; else name_.assign(reinterpret_cast<const char*>(d), len); name = true; break;
                case ModelBlock::Kit: if (kit) why = "two kits"; else kit_.assign(d, d + len); kit = true; break;
                default: why = "unknown block";
            }
            if (why) return why;
            p += size_t(len) + 9;
        }
        if (!meta || !norm || !bank || !name || !kit || nets_.size() != wantNets_) return "a part is missing";
        return nullptr;
    }
    const char* ReadMeta(const uint8_t* d, uint32_t len) {
        if (len != 20) return "bad shape";
        std::memcpy(&wantNets_, d, 4); std::memcpy(&hidden_, d + 4, 4); std::memcpy(&tokens_, d + 8, 4); std::memcpy(&variants_, d + 12, 4); std::memcpy(&played_, d + 16, 4);
        if (!wantNets_ || wantNets_ > kMaxNets || hidden_ < 4 || hidden_ > kMaxHidden || tokens_ < 1 || tokens_ > kMaxTokens || variants_ < 2 || variants_ > kMaxVariants) return "bad shape";
        if (!std::isfinite(played_) || played_ < 0) return "bad shape";
        return nullptr;
    }
    const char* ReadNorm(const uint8_t* d, uint32_t len) {
        if (len != netIn_ * 8) return "bad scale";
        std::memcpy(mean_.data(), d, netIn_ * 4); std::memcpy(inv_.data(), d + netIn_ * 4, netIn_ * 4);
        for (uint32_t i = 0; i < netIn_; ++i) {
            const float m = mean_[size_t(i)], v = inv_[size_t(i)];
            if (!std::isfinite(m) || !std::isfinite(v) || std::fabs(m) > kModelMaxNorm || v < 0 || v > kModelMaxNorm) return "bad scale";
        }
        return nullptr;
    }
    const char* ReadNet(const uint8_t* d, uint32_t len) {
        if (nets_.size() >= wantNets_) return "too many nets";
        const size_t h = hidden_, o = outputs(), n = size_t(netIn_) * h + h + h * h + h + h * o + o;
        if (len != n * 2) return "a net of the wrong shape";
        nets_.emplace_back();
        Net& net = nets_.back();
        size_t at = 0; bool ok = true;
        auto take = [&](std::vector<float>& v, size_t c) { v.resize(c); for (size_t i = 0; i < c; ++i, at += 2) { v[i] = detail::Half(d + at); ok = ok && std::isfinite(v[i]); } };
        take(net.w1, size_t(netIn_) * h); take(net.b1, h); take(net.w2, h * h); take(net.b2, h); take(net.w3, h * o); take(net.b3, o);
        return ok ? nullptr : "a weight that is not a number";
    }
    const char* ReadBank(const uint8_t* d, uint32_t len) {
        if (len < 2) return "bad bank";
        const uint32_t axes = uint32_t(d[0] | d[1] << 8);
        if (axes >> kAxisCount) return "bad bank";
        const size_t sampleBytes = 9 + 2 * size_t(std::popcount(axes)), clipBytes = 9 + 12, total = size_t(tokens_) * variants_;
        size_t p = 2;
        slots_.assign(total, -1);
        for (auto& h : has_) h.assign(tokens_, 0);
        for (size_t i = 0; i < total; ++i) {
            if (p >= len) return "bad bank";
            const uint32_t n = d[p++];
            if (!n) continue;
            if (n > kMaxClipSamples || len - p < clipBytes + n * sampleBytes) return "bad bank";
            ModelClip c{};
            for (int k = 0; k < 7; ++k) c.bit[k] = float(d[p + size_t(k)]) / 255.f;
            c.held = d[p + 7]; c.group = d[p + 8]; p += 9;
            if (c.group >= kWeaponGroups) return "a clip of no weapon";
            has_[c.group][i / variants_] = 1;
            float v[6];
            for (int k = 0; k < 6; ++k, p += 2) { v[k] = detail::Half(d + p); if (!std::isfinite(v[k])) return "a clip that is not a number"; }
            c.turn = v[0]; c.look = v[1]; c.prevTurn = v[2]; c.prevLook = v[3]; c.bearing = std::atan2(v[5], v[4]);
            c.first = uint32_t(samples_.size()); c.count = n;
            for (uint32_t j = 0; j < n; ++j) {
                Sample s{};
                s.dtMs = detail::Half(d + p);
                s.buttons = (uint32_t(d[p + 2]) | uint32_t(d[p + 3]) << 8 | uint32_t(d[p + 4]) << 16) & kPlayable;
                s.dyaw = detail::Half(d + p + 5); s.dpitch = detail::Half(d + p + 7);
                p += 9;
                bool ok = std::isfinite(s.dtMs) && std::isfinite(s.dyaw) && std::isfinite(s.dpitch);
                for (size_t a = 0; a < kAxisCount; ++a) {
                    if (!(axes >> a & 1u)) continue;
                    const float limit = IsDeltaAxis(Axis(a)) ? kAxisLimit : 1.f, x = detail::Half(d + p);
                    p += 2; ok = ok && std::isfinite(x);
                    s.axes[a] = std::clamp(x, -limit, limit);
                }
                if (!ok) return "a sample that is not a number";
                s.dtMs = std::clamp(s.dtMs, 1.f, kMaxDtMs);
                s.dyaw = std::clamp(s.dyaw, -kMaxTurnDeg, kMaxTurnDeg); s.dpitch = std::clamp(s.dpitch, -kMaxTurnDeg, kMaxTurnDeg);
                s.end = j + 1 == n;
                samples_.push_back(s);
            }
            slots_[i] = int(clips_.size());
            clips_.push_back(c);
        }
        if (p != len) return "bad bank";
        if (clips_.empty()) return "no clips";
        const uint32_t enough = std::max(1u, tokens_ / kThinGroup);
        for (int g = 0; g < kWeaponGroups; ++g) {
            moves_[size_t(g)].assign(tokens_, 0);
            uint32_t n = 0;
            for (uint8_t at : kGroupOrder[g]) {
                const uint32_t count = uint32_t(std::count(has_[at].begin(), has_[at].end(), uint8_t(1)));
                if (!count) continue;
                if (n && (g == 0 || at < kUntypedShort)) continue;
                plays_[size_t(g)] |= 1u << at; n += count;
                for (uint32_t t = 0; t < tokens_; ++t) moves_[size_t(g)][t] |= has_[at][t];
                if (n >= enough) break;
            }
        }
        return nullptr;
    }
    const char* error_ = nullptr;
    std::string name_;
    std::vector<uint8_t> kit_;
    uint32_t hidden_ = 0, tokens_ = 0, variants_ = 0, wantNets_ = 0, netIn_ = 0;
    float played_ = 0;
    std::array<float, kNetIn> mean_{}, inv_{};
    std::vector<Net> nets_;
    std::vector<ModelClip> clips_;
    std::vector<Sample> samples_;
    std::vector<int> slots_;
    std::array<std::vector<uint8_t>, kWeaponGroups> has_;     // per group: does this token have a clip
    std::array<std::vector<uint8_t>, kWeaponGroups> moves_;   // per group: has_ of all it plays
    std::array<uint32_t, kWeaponGroups> plays_{};             // per group: the groups it plays, a bit each
};
inline ModelScratch::ModelScratch(const Model& m) : z(size_t(kNetIn)), a(m.hidden()), b(m.hidden()), y(m.outputs()), sum(m.outputs()) {}

// Brain's twin: the same calls, the same walk through samples; the choice is the model's.
class ModelBrain {
public:
    ModelBrain(const Model& m, uint32_t seed) : m_(m), g_(seed), scratch_(m) {}
    void Reset(float yawDeg, float pitchDeg) {
        out_ = Output{}; out_.yaw = yawDeg; out_.pitch = pitchDeg;
        playing_ = false; cursor_ = last_ = first_ = 0; hideR_ = hideL_ = 0; budgetMs_ = sinceMs_ = fixDeg_ = 0; recent_.clear(); hist_.Reset();
        events_.Reset(true);
    }
    // An event of the clone's own fight, from the clone's side (kMe: the clone), as it happens.
    void Event(const FightEvent& e) { events_.Add(e); }
    bool NeedsLook() const { return !playing_ || sinceMs_ >= kModelDecideMs; }
    float PrevTurn() const { float v = 0; for (const Moved& m : recent_) v += m.turn; return v; }
    float PrevLook() const { float v = 0; for (const Moved& m : recent_) v += m.look; return v; }
    uint32_t buttons() const { return out_.buttons; }
    float guardR() const { return out_.axes[kGuardR]; }
    float guardL() const { return out_.axes[kGuardL]; }
    float yaw() const { return out_.yaw; }
    float pitch() const { return out_.pitch; }
    bool playing() const { return playing_; }
    uint32_t cursor() const { return cursor_; }
    // The camera is eased towards the clip's bearing to the foe: its input then lands where it did for the player.
    void Look(const float* seen) {
        float h[kHistory], ev[kEventInputs];
        hist_.Fill(h);
        events_.Fill(ev);
        const int c = m_.Pick(seen, h, g_, scratch_, ev);
        sinceMs_ = 0;
        if (c < 0) return;
        const ModelClip& clip = m_.clip(uint32_t(c));
        const float x = seen[kFoeX], y = seen[kFoeX + 1];
        const float d = std::hypot(x, y);
        fixDeg_ = std::isfinite(d) && d > .5f ? std::clamp(detail::Wrap((std::atan2(y, x) - clip.bearing) * 57.29578f), -kMaxTurnFixDeg, kMaxTurnFixDeg) : 0.f;
        cursor_ = clip.first; last_ = clip.first + clip.count; playing_ = true; budgetMs_ = 0;
        hideR_ = seen[kMyReach] > 0 ? GrabMask(&m_.sample(clip.first), clip.count, kGrabRight) : 0;
        hideL_ = seen[kMyReach + 1] > 0 || seen[kMyOffhand] > .5f ? GrabMask(&m_.sample(clip.first), clip.count, kGrabLeft) : 0;
        first_ = clip.first;
    }
    Output Step(float dtMs) {
        Output o = out_;
        o.axes[kTurn] = o.axes[kLook] = 0;
        sinceMs_ += dtMs;
        events_.Age(dtMs);
        const float share = std::min(1.f, dtMs / kModelDecideMs);
        o.yaw += fixDeg_ * share;
        fixDeg_ -= fixDeg_ * share;
        budgetMs_ += dtMs;
        for (uint32_t n = 0; playing_ && n < kMaxStepSamples && budgetMs_ >= m_.sample(cursor_).dtMs; ++n) {
            const Sample& s = m_.sample(cursor_);
            budgetMs_ -= s.dtMs;
            for (size_t a = 0; a < kAxisCount; ++a) o.axes[a] = IsDeltaAxis(Axis(a)) ? o.axes[a] + s.axes[a] : s.axes[a];
            o.yaw += s.dyaw; o.pitch += s.dpitch; o.buttons = s.buttons;
            if (hideR_ >> (cursor_ - first_) & 1u) o.buttons &= ~(1u << kGrabRight);
            if (hideL_ >> (cursor_ - first_) & 1u) o.buttons &= ~(1u << kGrabLeft);
            if (s.end || ++cursor_ >= last_) playing_ = false;
        }
        budgetMs_ = std::min(budgetMs_, kMaxDtMs);
        o.yaw = detail::Wrap(o.yaw);
        o.pitch = std::clamp(o.pitch, -80.f, 80.f);
        recent_.push_back({dtMs, o.axes[kTurn], o.axes[kLook]});
        float held = 0;
        for (const Moved& m : recent_) held += m.ms;
        while (held > kPrevMs && recent_.size() > 1) { held -= recent_.front().ms; recent_.pop_front(); }
        hist_.Add(dtMs, o.axes[kTurn], o.axes[kLook], o.buttons, o.axes[kGuardR], o.axes[kGuardL]);
        out_ = o;
        return o;
    }
private:
    static constexpr int kFoeX = 17, kGrabRight = int(Action::GrabRight), kGrabLeft = int(Action::GrabLeft);
    struct Moved { float ms, turn, look; };
    const Model& m_;
    ModelRng g_;
    ModelScratch scratch_;
    Output out_;
    InputHistory hist_;
    EventSense events_{true};
    bool playing_ = false;
    uint32_t cursor_ = 0, last_ = 0, first_ = 0;
    uint64_t hideR_ = 0, hideL_ = 0;   // the clip's samples whose grab stays out, GrabMask
    float budgetMs_ = 0, sinceMs_ = 0, fixDeg_ = 0;
    std::deque<Moved> recent_;
};

// [clone] brain=. auto takes the better of what there is, as the held-out test found it (training/data/split/RULE.md).
enum class BrainSetting { Auto, Model, Replay };
enum class BrainUse { None, Model, Replay };
constexpr bool kAutoPrefersModel = true;   // it won the one test run, narrowly: training/data/split/TEST_RESULT.txt
inline BrainSetting ParseBrain(std::string_view s) { return s == "model" ? BrainSetting::Model : s == "replay" ? BrainSetting::Replay : BrainSetting::Auto; }
inline BrainUse ChooseBrain(BrainSetting s, bool model, bool replay) {
    if (!model && !replay) return BrainUse::None;
    if (!model) return BrainUse::Replay;
    if (!replay) return BrainUse::Model;
    if (s == BrainSetting::Auto) return kAutoPrefersModel ? BrainUse::Model : BrainUse::Replay;
    return s == BrainSetting::Model ? BrainUse::Model : BrainUse::Replay;
}

}  // namespace hsm::clone
