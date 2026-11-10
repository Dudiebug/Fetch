// Merging recordings into one clone (the menu's Merge page, clonetrain --merge). Pure: the host reads the files and writes the
// result. The merged file uses no block kind a recording does not already have, so every Fetch loads it:
//   header · [Name A][A's blocks, Kit left out] · [Name B][B's blocks] … · [Name MERGED][Kit]
// Db::Load gives each Play block the Name in effect when it loads (Db::sources), so the clone knows whose play each move was.
// An older Fetch sees the last Name, the merged one, and all the play. Events, More, Input and Later are counted from the sample
// their Play block starts at, so they are copied as they are. Play recorded into the merged file later goes after its last Name:
// it is the merged clone's own.
#pragma once
#include "clone/hsm_clone.h"
#include "util/hsm_names.h"
#include <functional>
#include <string>
#include <vector>

namespace hsm::clone {

// A merged file stays within three quarters of what a file may hold, so it can still be recorded into (and trained).
constexpr uint64_t kMergeRows = uint64_t(kMaxRows) / 4 * 3, kMergeSamples = uint64_t(kMaxSamples) / 4 * 3,
                   kMergeEvents = uint64_t(kMaxEvents) / 4 * 3, kMergeBytes = uint64_t(kMaxWrite) / 4 * 3;
struct MergeLimits { uint64_t rows = kMergeRows, samples = kMergeSamples, events = kMergeEvents, bytes = kMergeBytes; };

struct MergeSource {
    std::string stem;   // the file's name without .hsmclone: the source's name if it has no Name block
    uint64_t size = 0;
    std::function<bool(uint64_t at, uint8_t* to, size_t n)> read;   // n bytes from `at`; false: could not
};

struct MergeResult {
    bool ok = false;
    std::string why;                  // ok: empty, or the sources left out and why; not ok: why nothing was made
    uint32_t used = 0, asked = 0;     // sources merged, of those given
    uint64_t rows = 0, samples = 0, bytes = 0;
    double seconds = 0;               // of play
    std::vector<std::string> names;   // the merged sources' names, in order
};

namespace detail {

// What MergeFiles takes from one source: where Db::Load would stop reading it, and what that much holds.
struct MergeScan {
    bool good = false;
    std::string why;
    uint64_t end = 0;                 // the end of the last block Load reads
    uint64_t rows = 0, samples = 0, events = 0, bytes = 0;   // bytes: what it adds to the merged file
    double seconds = 0;
    std::string name;                 // its last Name, else its stem
    bool many = false;                // it holds more than one source (a merged file): its Names are kept
    bool hasKit = false;
    std::vector<uint8_t> kit;         // its last Kit
};

inline MergeScan ScanSource(const MergeSource& src) {
    MergeScan s;
    const std::vector<uint8_t> head = FileHeader();
    std::vector<uint8_t> h(head.size()), b;
    if (src.size > kMaxFile) { s.why = "too big to load"; return s; }
    if (src.size < head.size() || !src.read(0, h.data(), h.size()) || !IsFileHeader(h.data(), h.size())) { s.why = "not a recording"; return s; }
    uint64_t names = 0, kits = 0, sources = 0, plays = 0;
    bool named = false;
    std::string lastName;
    for (uint64_t p = head.size(); src.size - p >= 9;) {
        uint8_t bh[5];
        if (!src.read(p, bh, 5)) break;
        uint32_t len, crc;
        std::memcpy(&len, bh + 1, 4);
        if (len > kMaxBlock || src.size - p - 9 < len) break;
        b.resize(size_t(len) + 4);
        if (!src.read(p + 5, b.data(), b.size())) break;
        std::memcpy(&crc, b.data() + len, 4);
        if (crc != util::Crc32(b.data(), len)) break;
        const uint8_t kind = bh[0];
        if (kind == uint8_t(Block::Play)) {
            uint32_t rows, samples;
            if (!Db::ValidPlay(b.data(), len, &rows, &samples) || s.rows + rows > kMaxRows || s.samples + samples > kMaxSamples) break;
            s.rows += rows; s.samples += samples; ++plays;
            const uint8_t* q = b.data() + 8 + size_t(rows) * (kFeatures * 4 + 4);
            for (uint32_t i = 0; i < samples; ++i) {
                Sample one;
                std::memcpy(&one, q + size_t(i) * sizeof(Sample), sizeof one);
                s.seconds += std::clamp(one.dtMs, 1.f, kMaxDtMs) / 1000.0;
            }
            if (!sources || named) ++sources;
            named = false;
        } else if (kind == uint8_t(Block::Events) || kind == uint8_t(Block::More) || kind == uint8_t(Block::Later)) {
            uint32_t n = 0;
            if (len >= 4) std::memcpy(&n, b.data(), 4);
            s.events += std::min<uint64_t>(n, (len - std::min<uint32_t>(len, 4)) / sizeof(FightEvent));   // at most what Load keeps
        } else if (kind == uint8_t(Block::Name)) {
            names += uint64_t(len) + 9;
            if (len <= kMaxName * 4) { lastName.assign(reinterpret_cast<const char*>(b.data()), len); named = true; }
        } else if (kind == uint8_t(Block::Kit)) {
            kits += uint64_t(len) + 9;
            s.kit.assign(b.begin(), b.begin() + len); s.hasKit = true;
        }
        p += uint64_t(len) + 9;
        s.end = p;
    }
    if (!plays) { s.why = "no play in it"; return s; }
    s.good = true;
    s.many = sources > 1;
    s.name = lastName.empty() ? src.stem : lastName;
    // What it adds: its leading Name, and its blocks but its Kits (and its Names, for one player's).
    s.bytes = (s.end - head.size()) - kits - (s.many ? 0 : names) + s.name.size() + 9;
    return s;
}

inline std::string Plural(uint64_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }

}  // namespace detail

// Merges `sources`, in the order given, into one file called `name`, written through `sink` from the start. The first source with
// a Kit gives the merged clone its look. A source is read where Db::Load would: to its first bad block. A source that does not
// fit within the limits (kMerge*), and every one after it, is left out whole. Refuses fewer than two good sources, two of one name,
// and a name one of them has. The host writes to a part file and keeps it only when ok (nothing is written when it is refused
// before the copy; one that fails while copying leaves a part file to delete).
// progress(writing, i, n): reading (false) or copying (true) source i of n.
inline MergeResult MergeFiles(const std::vector<MergeSource>& sources, const std::string& name,
                              const std::function<bool(const uint8_t*, size_t)>& sink,
                              const std::function<void(bool, uint32_t, uint32_t)>& progress = {}, const MergeLimits& limits = {}) {
    MergeResult r;
    r.asked = uint32_t(sources.size());
    if (sources.size() < 2) { r.why = "Pick two recordings or more"; return r; }
    const std::string out = FileStem(name);
    for (size_t i = 0; i < sources.size(); ++i) {
        if (SameNameAscii(sources[i].stem, out)) { r.why = "The merged clone needs a name of its own: " + sources[i].stem + " is one of those merged"; return r; }
        for (size_t j = 0; j < i; ++j)
            if (SameNameAscii(sources[i].stem, sources[j].stem)) { r.why = sources[i].stem + " is picked twice"; return r; }
    }
    std::vector<detail::MergeScan> scans;
    std::vector<size_t> use;
    std::string left;
    uint64_t rows = 0, samples = 0, events = 0, bytes = FileHeader().size() + name.size() + 9;   // with the merged Name
    bool haveKit = false;
    bool full = false;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (progress) progress(false, uint32_t(i), r.asked);
        scans.push_back(detail::ScanSource(sources[i]));
        const detail::MergeScan& s = scans.back();
        if (!s.good) { left += (left.empty() ? "" : "; ") + sources[i].stem + ": " + s.why; continue; }
        if (full) continue;
        const uint64_t kitBytes = !haveKit && s.hasKit ? s.kit.size() + 9 : 0;
        if (rows + s.rows > limits.rows || samples + s.samples > limits.samples || events + s.events > limits.events ||
            bytes + s.bytes + kitBytes > limits.bytes) {
            full = true;
            continue;
        }
        rows += s.rows; samples += s.samples; events += s.events; bytes += s.bytes + kitBytes;
        haveKit = haveKit || s.hasKit;
        use.push_back(i);
    }
    if (use.size() < 2) {
        r.why = use.empty() ? "None of them can be merged" : "Only one of them can be merged";
        if (!left.empty()) r.why += " (" + left + ")";
        else if (full) r.why += ": together they are too big for one clone";
        return r;
    }
    const std::vector<uint8_t>* kit = nullptr;
    for (size_t i : use) if (scans[i].hasKit) { kit = &scans[i].kit; break; }

    auto put = [&](const std::vector<uint8_t>& v) { r.bytes += v.size(); return sink(v.data(), v.size()); };
    auto putName = [&](const std::string& n) { return put(EncodeBlock(Block::Name, reinterpret_cast<const uint8_t*>(n.data()), n.size())); };
    if (!put(FileHeader())) { r.why = "Could not write the merged clone"; return r; }
    std::vector<uint8_t> chunk;
    constexpr size_t kChunk = size_t(1) << 20;
    for (size_t k = 0; k < use.size(); ++k) {
        const MergeSource& src = sources[use[k]];
        const detail::MergeScan& s = scans[use[k]];
        if (progress) progress(true, uint32_t(k), uint32_t(use.size()));
        if (!putName(s.name)) { r.why = "Could not write the merged clone"; return r; }
        // Its blocks as they are, but its Kit (one goes on the end) and, for one player's, its Names (the one above stands for
        // them). Load reads a block between a Play and its Events as the end of that play's events; a recorder never writes a
        // Name or Kit there, so leaving those out changes nothing a recorder wrote.
        for (uint64_t p = FileHeader().size(); p < s.end;) {
            uint8_t bh[5];
            uint32_t len;
            if (!src.read(p, bh, 5)) { r.why = "Could not read " + src.stem; return r; }
            std::memcpy(&len, bh + 1, 4);
            const uint64_t blockEnd = p + uint64_t(len) + 9;
            if (blockEnd > s.end) { r.why = src.stem + " changed while it was merged"; return r; }
            if (bh[0] == uint8_t(Block::Kit) || (bh[0] == uint8_t(Block::Name) && !s.many)) { p = blockEnd; continue; }
            while (p < blockEnd) {
                chunk.resize(size_t(std::min<uint64_t>(kChunk, blockEnd - p)));
                if (!src.read(p, chunk.data(), chunk.size())) { r.why = "Could not read " + src.stem; return r; }
                if (!put(chunk)) { r.why = "Could not write the merged clone"; return r; }
                p += chunk.size();
            }
        }
        r.rows += s.rows; r.samples += s.samples; r.seconds += s.seconds;
        r.names.push_back(s.name);
    }
    if (!putName(name)) { r.why = "Could not write the merged clone"; return r; }
    if (kit && !put(EncodeBlock(Block::Kit, kit->data(), kit->size()))) { r.why = "Could not write the merged clone"; return r; }
    r.used = uint32_t(use.size());
    r.ok = true;
    if (r.used < r.asked) {
        r.why = "Merged " + std::to_string(r.used) + " of " + std::to_string(r.asked);
        if (full) r.why += std::string(": the rest would make it too big for one clone");
        if (!left.empty()) r.why += (full ? "; " : ": ") + left;
    }
    return r;
}

}  // namespace hsm::clone
