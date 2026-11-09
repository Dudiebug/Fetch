// clonetrain: Clones/<name>.hsmclone in, Clones/<name>.hsmmodel out. Runs on the CPU at low priority, outside the game, and
// stops inside its time limit. No installs, no GPU, no network. docs/CLONE.md.
#include "clone/hsm_train.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace hsm::clone;

static void LowPriority() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#else
    if (nice(10) == -1) std::puts("could not lower priority");
#endif
}
static int Usage() {
    std::puts("usage: clonetrain NAME.hsmclone [OUT.hsmmodel] [--threads N] [--minutes M] [--outcome X]\n"
              "  OUT defaults to NAME.hsmmodel beside the clone file.\n"
              "  --threads  CPU threads to use (default: half of them, at most 8)\n"
              "  --minutes  time limit (default 4.5; a long recording gets fewer passes, never a late finish)\n"
              "  --outcome  how much what came of a move weighs it: hits landed up, hits taken down (default 1; 0: not at all)");
    return 2;
}

int main(int argc, char** argv) {
    train::Options o;
    const unsigned hw = std::thread::hardware_concurrency();
    o.threads = int(std::clamp(hw / 2, 1u, 8u));
    std::string in, out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--threads" && i + 1 < argc) o.threads = std::clamp(std::atoi(argv[++i]), 1, 64);
        else if (a == "--minutes" && i + 1 < argc) o.budgetS = std::clamp(std::atof(argv[++i]), .1, 600.) * 60;
        else if (a == "--outcome" && i + 1 < argc) o.outcome = float(std::clamp(std::atof(argv[++i]), 0., 4.));
        else if (a == "--scalar") o.forceScalar = true;
        else if (a.rfind("--", 0) == 0) return Usage();
        else if (in.empty()) in = a;
        else if (out.empty()) out = a;
        else return Usage();
    }
    if (in.empty()) return Usage();
    if (out.empty()) out = std::filesystem::path(in).replace_extension(".hsmmodel").string();
    LowPriority();
    std::ifstream f(in, std::ios::binary);
    if (!f) { std::printf("cannot read %s\n", in.c_str()); return 1; }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Db db;
    if (!db.Load(bytes.data(), bytes.size())) { std::printf("%s is not a clone file with play in it\n", in.c_str()); return 1; }
    std::printf("%s: %.1f hours of play, %u rows, %d threads, limit %.1f min\n", db.name().c_str(), db.PlayedSeconds() / 3600, db.rows(), o.threads, o.budgetS / 60);
    ModelParts parts; train::Report rep; std::string why;
    const auto say = [](const std::string& s) { std::puts(s.c_str()); std::fflush(stdout); };
    if (!train::Run(db, o, parts, rep, why, say)) { std::printf("no model: %s\n", why.c_str()); return 1; }
    const std::vector<uint8_t> file = EncodeModel(parts);
    Model check;
    if (!check.Load(file.data(), file.size())) { std::printf("no model: it would not load (%s)\n", check.error()); return 1; }
    const std::string tmp = out + ".part";
    { std::ofstream w(tmp, std::ios::binary); w.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size())); if (!w) { std::printf("cannot write %s\n", tmp.c_str()); return 1; } }
    std::error_code ec; std::filesystem::rename(tmp, out, ec);
    if (ec) { std::printf("cannot write %s: %s\n", out.c_str(), ec.message().c_str()); return 1; }
    std::printf("%s: %.2f MB, %.1f s (moves %.1f s, nets %.1f s), last loss %.3f%s\n", out.c_str(), double(file.size()) / 1048576, rep.seconds, rep.clusterS, rep.netS, rep.loss,
                rep.cut ? ", cut short by the time limit" : "");
    return 0;
}
