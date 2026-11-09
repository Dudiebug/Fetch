// Turns a clone file into a model: tools/clonetrain's whole job, and the mod never runs it. It groups the player's 400 ms of
// input into move tokens, trains small nets to say which token comes next and what changes in the next 300 ms, and keeps real
// clips of each token. Plain C++ and std::thread; AVX2 if the CPU has it, picked at run time. A hard limit on time: the
// schedule shrinks to fit, so a long recording costs quality, never a late finish. Adaptive Rival shares none of this.
#pragma once
#include "clone/hsm_model.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <random>
#include <thread>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define HSM_TRAIN_X86 1
#endif

namespace hsm::clone::train {

struct Options {
    int threads = 4, nets = 3, epochs = 12, batch = 1024;
    uint32_t tokens = 1024, hidden = 256, variants = 16;
    float noise = .25f, aux = .5f, lr = 2e-3f;
    float outcome = 1.f;   // how much a move's outcome (hits landed, hits taken) weighs it in training; 0: not at all
    float clipMs = 400, keepMs = 240, summaryMs = 300;   // a token's length; what a clip keeps (past kModelDecideMs); what the summary sees
    double budgetS = 270;
    uint32_t seed = 3;
    uint32_t minRows = 1500;   // about 80 s of fighting
    bool forceScalar = false;
    std::vector<float>* centres = nullptr;   // the lab's: the tokens' centres, to score held-out play by token
    const std::vector<uint8_t>* hears = nullptr;   // the lab's: which inputs the nets get, in place of NetHears
};
inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline void Pause() {
#ifdef HSM_TRAIN_X86
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

// A few threads that stay up between the thousands of small jobs of a training run.
class Pool {
public:
    explicit Pool(int n) : n_(std::max(n, 1)) { for (int i = 1; i < n_; ++i) t_.emplace_back([this, i] { Loop(i); }); }
    ~Pool() { stop_ = true; gen_.fetch_add(1); for (auto& t : t_) t.join(); }
    int size() const { return n_; }
    void Run(const std::function<void(int)>& f) {
        f_ = &f; left_.store(n_ - 1); gen_.fetch_add(1);
        f(0);
        while (left_.load(std::memory_order_acquire)) Pause();
    }
    void For(size_t n, const std::function<void(size_t, size_t)>& f) {
        Run([&](int t) { const size_t a = n * size_t(t) / size_t(n_), b = n * size_t(t + 1) / size_t(n_); if (a < b) f(a, b); });
    }
private:
    void Loop(int id) {
        uint64_t seen = 0;
        for (;;) {
            int spin = 0;
            while (gen_.load(std::memory_order_acquire) == seen) { if (++spin > 2000) std::this_thread::yield(); else Pause(); }
            seen = gen_.load();
            if (stop_) return;
            (*f_)(id);
            left_.fetch_sub(1, std::memory_order_release);
        }
    }
    int n_;
    std::vector<std::thread> t_;
    const std::function<void(int)>* f_ = nullptr;
    std::atomic<uint64_t> gen_{0};
    std::atomic<int> left_{0};
    std::atomic<bool> stop_{false};
};

// C[M,N] (+)= A[M,K] B[K,N]; A read through strides, so a transpose costs nothing. N a multiple of 16.
inline void GemmPlain(int M, int N, int K, const float* A, int am, int ak, const float* B, int ldb, float* C, int ldc, bool acc) {
    for (int m = 0; m < M; ++m) {
        float* c = C + size_t(m) * size_t(ldc);
        if (!acc) std::fill(c, c + N, 0.f);
        for (int k = 0; k < K; ++k) {
            const float x = A[size_t(m) * size_t(am) + size_t(k) * size_t(ak)];
            const float* b = B + size_t(k) * size_t(ldb);
            for (int n = 0; n < N; ++n) c[n] += x * b[n];
        }
    }
}
#ifdef HSM_TRAIN_X86
__attribute__((target("avx2,fma"))) inline void GemmAvx2(int M, int N, int K, const float* A, int am, int ak, const float* B, int ldb, float* C, int ldc, bool acc) {
    for (int n = 0; n < N; n += 16) {
        int m = 0;
        for (; m + 6 <= M; m += 6) {
            __m256 c[6][2];
            for (int i = 0; i < 6; ++i) for (int j = 0; j < 2; ++j) c[i][j] = acc ? _mm256_loadu_ps(C + size_t(m + i) * size_t(ldc) + size_t(n + 8 * j)) : _mm256_setzero_ps();
            const float* a = A + size_t(m) * size_t(am);
            const float* b = B + n;
            for (int k = 0; k < K; ++k, a += ak, b += ldb) {
                const __m256 b0 = _mm256_loadu_ps(b), b1 = _mm256_loadu_ps(b + 8);
                for (int i = 0; i < 6; ++i) { const __m256 x = _mm256_broadcast_ss(a + size_t(i) * size_t(am)); c[i][0] = _mm256_fmadd_ps(x, b0, c[i][0]); c[i][1] = _mm256_fmadd_ps(x, b1, c[i][1]); }
            }
            for (int i = 0; i < 6; ++i) for (int j = 0; j < 2; ++j) _mm256_storeu_ps(C + size_t(m + i) * size_t(ldc) + size_t(n + 8 * j), c[i][j]);
        }
        for (; m < M; ++m) {
            float* cp = C + size_t(m) * size_t(ldc) + size_t(n);
            __m256 c0 = acc ? _mm256_loadu_ps(cp) : _mm256_setzero_ps(), c1 = acc ? _mm256_loadu_ps(cp + 8) : _mm256_setzero_ps();
            const float* a = A + size_t(m) * size_t(am);
            const float* b = B + n;
            for (int k = 0; k < K; ++k, a += ak, b += ldb) { const __m256 x = _mm256_broadcast_ss(a); c0 = _mm256_fmadd_ps(x, _mm256_loadu_ps(b), c0); c1 = _mm256_fmadd_ps(x, _mm256_loadu_ps(b + 8), c1); }
            _mm256_storeu_ps(cp, c0); _mm256_storeu_ps(cp + 8, c1);
        }
    }
}
inline bool HaveAvx2() { return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"); }
#else
inline bool HaveAvx2() { return false; }
#endif
struct Cpu {
    bool avx2;
    explicit Cpu(bool scalar = false) : avx2(!scalar && HaveAvx2()) {}
    void Gemm(int M, int N, int K, const float* A, int am, int ak, const float* B, int ldb, float* C, int ldc, bool acc) const {
#ifdef HSM_TRAIN_X86
        if (avx2) { GemmAvx2(M, N, K, A, am, ak, B, ldb, C, ldc, acc); return; }
#endif
        GemmPlain(M, N, K, A, am, ak, B, ldb, C, ldc, acc);
    }
};

// Assigns each row of X to its closest centre, by |c|^2 - 2 x.c, a block of rows at a time.
inline void Assign(Pool& pool, const Cpu& cpu, const std::vector<float>& X, size_t n, int dim, int K, const std::vector<float>& C, std::vector<int>& as) {
    const int kp = (K + 15) / 16 * 16;
    std::vector<float> Ct(size_t(dim) * size_t(kp), 0.f), cn(static_cast<size_t>(kp), 3.4e38f);
    for (int k = 0; k < K; ++k) { float s = 0; for (int j = 0; j < dim; ++j) { const float c = C[size_t(k) * size_t(dim) + size_t(j)]; Ct[size_t(j) * size_t(kp) + size_t(k)] = -2 * c; s += c * c; } cn[size_t(k)] = s; }
    as.resize(n);
    pool.For(n, [&](size_t a, size_t b) {
        constexpr int kBlock = 96;
        std::vector<float> D(size_t(kBlock) * size_t(kp));
        for (size_t r = a; r < b; r += kBlock) {
            const int m = int(std::min<size_t>(kBlock, b - r));
            cpu.Gemm(m, kp, dim, &X[r * size_t(dim)], dim, 1, Ct.data(), kp, D.data(), kp, false);
            for (int i = 0; i < m; ++i) {
                const float* d = &D[size_t(i) * size_t(kp)]; float best = 3.4e38f; int bi = 0;
                for (int k = 0; k < K; ++k) { const float e = d[k] + cn[size_t(k)]; if (e < best) { best = e; bi = k; } }
                as[r + size_t(i)] = bi;
            }
        }
    });
}
// k-means on at most `sample` rows, then every row assigned once. Stops early at `deadline`.
inline std::vector<int> KMeans(Pool& pool, const Cpu& cpu, const std::vector<float>& X, size_t n, int dim, int K, int iters, std::vector<float>& C, uint32_t seed, double deadline = 1e300, size_t sample = 400000) {
    std::mt19937 g(seed);
    std::vector<float> S; const std::vector<float>* use = &X; size_t ns = n;
    if (n > sample) {
        S.resize(sample * size_t(dim)); ns = sample;
        for (size_t i = 0; i < sample; ++i) std::memcpy(&S[i * size_t(dim)], &X[size_t(g() % n) * size_t(dim)], size_t(dim) * 4);
        use = &S;
    }
    C.assign(size_t(K) * size_t(dim), 0.f);
    for (int k = 0; k < K; ++k) std::memcpy(&C[size_t(k) * size_t(dim)], &(*use)[size_t(g() % ns) * size_t(dim)], size_t(dim) * 4);
    std::vector<int> as;
    for (int it = 0; it < iters && Now() < deadline; ++it) {
        Assign(pool, cpu, *use, ns, dim, K, C, as);
        std::vector<double> sum(size_t(K) * size_t(dim), 0.); std::vector<int> cnt(static_cast<size_t>(K), 0);
        for (size_t i = 0; i < ns; ++i) { const size_t k = size_t(as[i]); cnt[k]++; for (int j = 0; j < dim; ++j) sum[k * size_t(dim) + size_t(j)] += (*use)[i * size_t(dim) + size_t(j)]; }
        for (int k = 0; k < K; ++k) {
            float* c = &C[size_t(k) * size_t(dim)];
            if (cnt[size_t(k)]) for (int j = 0; j < dim; ++j) c[j] = float(sum[size_t(k) * size_t(dim) + size_t(j)] / cnt[size_t(k)]);
            else std::memcpy(c, &(*use)[size_t(g() % ns) * size_t(dim)], size_t(dim) * 4);   // an empty token takes a new seat
        }
    }
    Assign(pool, cpu, X, n, dim, K, C, as);
    return as;
}

// A net: ReLU hidden layers, linear out; widths padded to 16 for the kernels.
struct Mlp {
    std::vector<int> sz, pad;
    std::vector<std::vector<float>> W, B;   // W[l]: in x padOut
    void Init(std::vector<int> s, uint32_t seed) {
        sz = s; pad.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) pad[i] = (s[i] + 15) / 16 * 16;
        W.resize(s.size() - 1); B.resize(s.size() - 1);
        std::mt19937 g(seed);
        for (size_t l = 0; l + 1 < s.size(); ++l) {
            W[l].assign(size_t(pad[l]) * size_t(pad[l + 1]), 0.f); B[l].assign(size_t(pad[l + 1]), 0.f);
            std::normal_distribution<float> n(0, std::sqrt(2.f / float(s[l])));
            for (int i = 0; i < s[l]; ++i) for (int j = 0; j < s[l + 1]; ++j) W[l][size_t(i) * size_t(pad[l + 1]) + size_t(j)] = n(g);
        }
    }
    void Forward(const Cpu& cpu, const float* X, int n, std::vector<std::vector<float>>& acts) const {
        const int L = int(W.size());
        acts.resize(size_t(L) + 1);
        for (int l = 0; l < L; ++l) {
            const size_t ls = size_t(l), wo = size_t(pad[ls + 1]);
            const float* in = l ? acts[ls].data() : X;
            auto& o = acts[ls + 1]; o.resize(size_t(n) * wo);
            cpu.Gemm(n, pad[ls + 1], pad[ls], in, pad[ls], 1, W[ls].data(), pad[ls + 1], o.data(), pad[ls + 1], false);
            for (int r = 0; r < n; ++r) { float* y = &o[size_t(r) * wo]; for (size_t j = 0; j < wo; ++j) { y[j] += B[ls][j]; if (l + 1 < L && y[j] < 0) y[j] = 0; } }
        }
    }
};
struct Adam { float lr, c1, c2, inv; };
#if defined(__clang__) || defined(__GNUC__)
#define HSM_INLINE inline __attribute__((always_inline))
#else
#define HSM_INLINE inline
#endif
HSM_INLINE void AdamBody(float* w, float* m1, float* m2, const float* const* g, int T, size_t n, Adam a) {
    for (size_t i = 0; i < n; ++i) {
        float s = 0;
        for (int t = 0; t < T; ++t) s += g[t][i];
        s *= a.inv;
        m1[i] = .9f * m1[i] + .1f * s; m2[i] = .999f * m2[i] + .001f * s * s;
        w[i] -= a.lr * (m1[i] / a.c1) / (std::sqrt(m2[i] / a.c2) + 1e-8f);
    }
}
inline void AdamPlain(float* w, float* m1, float* m2, const float* const* g, int T, size_t n, Adam a) { AdamBody(w, m1, m2, g, T, n, a); }
#ifdef HSM_TRAIN_X86
__attribute__((target("avx2,fma"))) inline void AdamAvx2(float* w, float* m1, float* m2, const float* const* g, int T, size_t n, Adam a) { AdamBody(w, m1, m2, g, T, n, a); }
#endif

using LossFn = std::function<float(uint32_t, const float*, float*)>;   // row, outputs, gradient to fill; the loss
struct Schedule { int epochs, batch; float lr, noise; uint32_t seed; double deadline; bool* cut = nullptr; };
// Adam, cosine rate. The run is cut to what the time allows after each epoch, so the rate still reaches its end. Returns the last epoch's mean loss.
inline double Fit(Pool& pool, const Cpu& cpu, Mlp& m, const std::vector<float>& X, const std::vector<uint32_t>& rows, const LossFn& loss, const Schedule& o) {
    const int L = int(m.W.size()), T = pool.size(), in = m.pad[0], outW = m.pad[size_t(L)];
    size_t params = 0; std::vector<size_t> offW(static_cast<size_t>(L)), offB(static_cast<size_t>(L));
    for (size_t l = 0; l < size_t(L); ++l) { offW[l] = params; params += m.W[l].size(); offB[l] = params; params += m.B[l].size(); }
    std::vector<float> M1(params, 0.f), M2(params, 0.f);
    std::vector<std::vector<float>> G(static_cast<size_t>(T), std::vector<float>(params, 0.f));
    std::vector<const float*> gp(static_cast<size_t>(T));
    std::vector<uint32_t> order = rows; std::mt19937 g(o.seed);
    std::vector<double> lossT(static_cast<size_t>(T), 0.);
    std::vector<std::vector<float>> Wt(static_cast<size_t>(L));
    for (size_t l = 0; l < size_t(L); ++l) Wt[l].resize(m.W[l].size());
    std::vector<std::mt19937> rg; for (int t = 0; t < T; ++t) rg.emplace_back(o.seed * 7919u + uint32_t(t));
    struct Room { std::vector<float> xb, dA, dB; std::vector<std::vector<float>> acts; };
    std::vector<Room> room(static_cast<size_t>(T));
    const int64_t stepsPer = int64_t((order.size() + size_t(o.batch) - 1) / size_t(o.batch));
    int64_t total = std::max<int64_t>(1, stepsPer * o.epochs), step = 0;
    const double t0 = Now();
    double last = 0;
    while (step < total) {
        std::shuffle(order.begin(), order.end(), g);
        double sum = 0; size_t seen = 0;
        for (size_t at = 0; at < order.size() && step < total; at += size_t(o.batch)) {
            const int bn = int(std::min<size_t>(size_t(o.batch), order.size() - at));
            for (size_t l = 0; l < size_t(L); ++l) {
                const size_t a = size_t(m.pad[l]), b = size_t(m.pad[l + 1]);
                pool.For(a, [&](size_t x, size_t y) { for (size_t i = x; i < y; ++i) for (size_t j = 0; j < b; ++j) Wt[l][j * a + i] = m.W[l][i * b + j]; });
            }
            pool.Run([&](int t) {
                const size_t ts = size_t(t);
                const int a = bn * t / T, b = bn * (t + 1) / T, n = b - a;
                float* gr = G[ts].data();
                std::memset(gr, 0, params * 4);
                lossT[ts] = 0;
                if (n <= 0) return;
                Room& r = room[ts];
                r.xb.resize(size_t(n) * size_t(in));
                std::normal_distribution<float> nz(0, 1);
                for (int i = 0; i < n; ++i) {
                    float* x = &r.xb[size_t(i) * size_t(in)];
                    std::memcpy(x, &X[size_t(order[at + size_t(a + i)]) * size_t(in)], size_t(in) * 4);
                    if (o.noise > 0) for (int k = 0; k < m.sz[0]; ++k) x[k] += o.noise * nz(rg[ts]);
                }
                m.Forward(cpu, r.xb.data(), n, r.acts);
                r.dA.resize(size_t(n) * size_t(outW));
                double ls = 0;
                for (int i = 0; i < n; ++i) ls += loss(order[at + size_t(a + i)], &r.acts[size_t(L)][size_t(i) * size_t(outW)], &r.dA[size_t(i) * size_t(outW)]);
                lossT[ts] = ls;
                for (int l = L - 1; l >= 0; --l) {
                    const size_t ls2 = size_t(l); const int wi = m.pad[ls2], wo = m.pad[ls2 + 1];
                    const float* xin = l ? r.acts[ls2].data() : r.xb.data();
                    cpu.Gemm(wi, wo, n, xin, 1, wi, r.dA.data(), wo, gr + offW[ls2], wo, false);
                    float* gb = gr + offB[ls2];
                    for (int i = 0; i < n; ++i) for (int j = 0; j < wo; ++j) gb[j] += r.dA[size_t(i) * size_t(wo) + size_t(j)];
                    if (l) {
                        r.dB.resize(size_t(n) * size_t(wi));
                        cpu.Gemm(n, wi, wo, r.dA.data(), wo, 1, Wt[ls2].data(), wi, r.dB.data(), wi, false);
                        for (size_t i = 0; i < r.dB.size(); ++i) if (xin[i] <= 0) r.dB[i] = 0;
                        r.dA.swap(r.dB);
                    }
                }
            });
            for (int t = 0; t < T; ++t) { sum += lossT[size_t(t)]; gp[size_t(t)] = G[size_t(t)].data(); }
            seen += size_t(bn);
            ++step;
            const float frac = float(step) / float(total);
            const float lr = o.lr * (step < 100 ? float(step) / 100 : .5f * (1 + std::cos(3.14159265f * frac)) * .98f + .02f);
            const Adam ad{lr, 1 - std::pow(.9f, float(step)), 1 - std::pow(.999f, float(step)), 1.f / float(bn)};
            pool.For(params, [&](size_t x, size_t y) {
                for (size_t l = 0; l < size_t(L); ++l) for (int part = 0; part < 2; ++part) {
                    const size_t off = part ? offB[l] : offW[l], n = part ? m.B[l].size() : m.W[l].size();
                    const size_t a = std::max(x, off), b = std::min(y, off + n);
                    if (a >= b) continue;
                    float* w = (part ? m.B[l].data() : m.W[l].data()) + (a - off);
                    std::vector<const float*> gs(gp); for (auto& p : gs) p += a;
#ifdef HSM_TRAIN_X86
                    if (cpu.avx2) { AdamAvx2(w, M1.data() + a, M2.data() + a, gs.data(), T, b - a, ad); continue; }
#endif
                    AdamPlain(w, M1.data() + a, M2.data() + a, gs.data(), T, b - a, ad);
                }
            });
            if (Now() >= o.deadline) { if (step < total && o.cut) *o.cut = true; total = step; break; }
        }
        last = sum / double(std::max<size_t>(1, seen));
        const double per = (Now() - t0) / double(step);
        const double more = (o.deadline - Now()) / std::max(per, 1e-9);
        if (more < double(total - step)) { total = step + int64_t(std::max(more, 0.)); if (o.cut) *o.cut = true; }
    }
    return last;
}

// ---------------------------------------------------------------- the data
struct Data {
    const Db& db;
    std::vector<uint32_t> stretchEnd;   // sample -> last sample of its stretch of play
    explicit Data(const Db& d) : db(d), stretchEnd(d.samples()) {
        uint32_t e = d.samples() - 1;
        for (uint32_t i = d.samples(); i-- > 0;) { if (d.sample(i).end) e = i; stretchEnd[i] = e; }
    }
};
constexpr int kRare[] = {0, 1, 4, 6, 8, 10, 17};   // grabs, thrust, jump, crouch, swap, right mouse: rare, and they make a move what it is
constexpr int kRareCount = 7;
inline float Squeeze(float x) { return (x < 0 ? -1.f : 1.f) * std::log1p(std::fabs(x) / 4.f); }
inline int DescribeSize(float ms) { return 14 + 2 * int(ms / 100) + kRareCount; }
inline float BitOf(const Sample& s, int b) {   // W S A D guardR guardL run
    return b < 4 ? float(s.buttons >> (kKeyBit + b) & 1u) : b == 4 ? float(s.axes[kGuardR] > .5f) : b == 5 ? float(s.axes[kGuardL] > .5f) : float(s.buttons >> kRunBit & 1u);
}
// A clip as numbers, so like moves land close. false: its stretch of play ends too soon.
inline bool Describe(const Data& d, uint32_t from, float ms, float* o) {
    const int bins = int(ms / 100);
    const uint32_t end = d.stretchEnd[from];
    float held = 0, on[2][7] = {}, hs[2] = {}, mt[16] = {}, ml[16] = {}, rare[kRareCount] = {};
    for (uint32_t i = from; held < ms; ++i) {
        if (i > end) return false;
        const Sample& s = d.db.sample(i);
        const int h = held < ms / 2 ? 0 : 1, b = std::min(bins - 1, int(held / 100));
        for (int k = 0; k < 7; ++k) on[h][k] += BitOf(s, k) * s.dtMs;
        hs[h] += s.dtMs;
        mt[b] += s.axes[kTurn]; ml[b] += s.axes[kLook];
        for (int k = 0; k < kRareCount; ++k) if (s.buttons >> kRare[k] & 1u) rare[k] = 1;
        held += s.dtMs;
    }
    int k = 0;
    for (int h = 0; h < 2; ++h) for (int j = 0; j < 7; ++j) o[k++] = on[h][j] / std::max(1.f, hs[h]);
    for (int b = 0; b < bins; ++b) { o[k++] = Squeeze(mt[b]); o[k++] = Squeeze(ml[b]); }
    for (int j = 0; j < kRareCount; ++j) o[k++] = rare[j];
    return true;
}
struct Next { float bit[7], turn, look; };
inline bool Summarise(const Data& d, uint32_t from, float ms, Next& n) {
    n = Next{}; float held = 0, on[7] = {};
    const uint32_t end = d.stretchEnd[from];
    for (uint32_t i = from; held < ms; ++i) {
        if (i > end) return false;
        const Sample& s = d.db.sample(i);
        held += s.dtMs; n.turn += s.axes[kTurn]; n.look += s.axes[kLook];
        for (int k = 0; k < 7; ++k) on[k] += BitOf(s, k) * s.dtMs;
    }
    for (int k = 0; k < 7; ++k) n.bit[k] = on[k] / held;
    return true;
}
// InputHistory as it stood at each row, as the fight went.
inline std::vector<float> HistoryAtRows(const Db& db) {
    std::vector<float> E(size_t(db.rows()) * kHistory); InputHistory h; uint32_t r = 0;
    for (uint32_t i = 0; i < db.samples(); ++i) {
        while (r < db.rows() && db.start(r) <= i) { h.Fill(&E[size_t(r) * kHistory]); ++r; }
        const Sample& s = db.sample(i);
        h.Add(s.dtMs, s.axes[kTurn], s.axes[kLook], s.buttons, s.axes[kGuardR], s.axes[kGuardL]);
        if (s.end) h.Reset();
    }
    return E;
}
// A stretch of play starts at 0 and after every sample that ends one. An event stamped with a stretch's first sample came before
// it: after the stretch before ended (a death, the end of a block), so it is that one's.
inline bool StretchStart(const Db& db, uint32_t i) { return i == 0 || db.sample(i - 1).end; }

// EventSense at each row, as the clone hears its own fight: what happened before the row was seen. A row's start is the sample
// after the one it was seen with, so an event stamped with it came after. Rows of play without events read as not known.
inline std::vector<float> EventsAtRows(const Db& db) {
    std::vector<float> E(size_t(db.rows()) * kEventInputs);
    const std::vector<FightEvent>& ev = db.events();
    EventSense sense(db.samples() && db.EventsKnown(0));
    size_t e = 0; uint32_t r = 0;
    for (uint32_t i = 0; i < db.samples(); ++i) {
        while (r < db.rows() && db.start(r) <= i) { sense.Fill(&E[size_t(r) * kEventInputs]); ++r; }
        const bool skip = StretchStart(db, i);
        for (; e < ev.size() && ev[e].sample <= i; ++e) if (!skip || ev[e].sample < i) sense.Add(ev[e]);
        const Sample& x = db.sample(i);
        sense.Age(x.dtMs);
        if (x.end) sense.Reset(i + 1 < db.samples() && db.EventsKnown(i + 1));
    }
    for (; r < db.rows(); ++r) sense.Fill(&E[size_t(r) * kEventInputs]);
    return E;
}

// Each row's weight in training, by what came of the move (OutcomeWeight): a hit landed soon after counts it up, one taken down.
// Rows without events weigh 1. Scaled so the weights average 1, which keeps the learning rate what it was. good: how many rows
// led to something good, for the report.
inline std::vector<float> OutcomeAtRows(const Data& d, float strength, uint32_t* good = nullptr, uint32_t* known = nullptr) {
    const Db& db = d.db;
    std::vector<float> W(db.rows(), 1.f);
    if (good) *good = 0;
    if (known) *known = 0;
    if (!(strength > 0) || db.events().empty()) return W;
    std::vector<double> at(size_t(db.samples()) + 1, 0.);   // ms of play before each sample
    for (uint32_t i = 0; i < db.samples(); ++i) at[i + 1] = at[i] + double(db.sample(i).dtMs);
    const std::vector<FightEvent>& ev = db.events();
    double sum = 0;
    for (uint32_t r = 0; r < db.rows(); ++r) {
        const uint32_t from = db.start(r);
        if (from >= db.samples() || !db.EventsKnown(from)) { sum += 1; continue; }
        if (known) ++*known;
        const uint32_t last = d.stretchEnd[from] + 1;   // the stretch's own, and those stamped right after it
        float g = 0, b = 0;
        auto it = std::lower_bound(ev.begin(), ev.end(), from, [](const FightEvent& x, uint32_t v) { return x.sample < v; });
        for (; it != ev.end() && it->sample <= last; ++it) {
            const double ms = at[std::min(it->sample, db.samples())] - at[from];
            if (ms > kOutcomeAheadMs) break;
            AddOutcome(*it, float(ms), g, b);
        }
        W[r] = OutcomeWeight(g, b, strength);
        if (good && g > b) ++*good;
        sum += W[r];
    }
    const float scale = sum > 0 ? float(double(db.rows()) / sum) : 1.f;
    for (float& w : W) w *= scale;
    return W;
}

inline float SoftmaxLoss(const float* y, float* g, int K, int outW, int target) {
    float mx = -1e30f;
    for (int j = 0; j < K; ++j) mx = std::max(mx, y[j]);
    float z = 0;
    for (int j = 0; j < K; ++j) { g[j] = std::exp(y[j] - mx); z += g[j]; }
    const float iz = 1 / z;
    for (int j = 0; j < K; ++j) g[j] *= iz;
    for (int j = K; j < outW; ++j) g[j] = 0;
    const float p = g[target];
    g[target] -= 1;
    return -std::log(std::max(p, 1e-20f));
}

struct Report { uint32_t rows = 0, usable = 0, tokens = 0, eventRows = 0, goodRows = 0; double seconds = 0, clusterS = 0, netS = 0; float loss = 0; bool cut = false; };
using Say = std::function<void(const std::string&)>;
// false, with why set: too little play to learn from. The result is ready for EncodeModel.
// What the nets get. Past what they saw at first, each input has to earn its place on held-out fights (docs/CLONE.md): with hours
// of play and not years, most of them only tell the nets which fight this is. The foe's wounds earned theirs, and so did the type
// of the fighter's own weapons, which also brought back the mouse that splitting moves by type had cost. An input left out scales to 0.
// Model 5: what both wear too, now that it is read (hsm_clone_armor.h); play recorded before then has it zeroed (Db::Load), so it
// tells the nets only about fights where it was known.
inline bool NetHears(int input) {
    return input < 42 || (input >= kFoeHealth && input < kFoeHealth + 12) || input == kMyType || input == kMyType + 1 ||
           (input >= kFoeArmor && input < kFoeArmor + kParts) || (input >= kMyArmor && input < kMyArmor + kParts) || input == kArmorKnown ||
           input >= kFeatures;
}

// How the moves go round the weapon lengths played: every length gets one, then the rest by how much of it there was.
inline void Allot(const uint32_t (&n)[kWeaponGroups], uint32_t slots, std::array<uint32_t, kWeaponGroups>& out) {
    out.fill(0);
    uint32_t left = slots;
    for (int g = 0; g < kWeaponGroups && left; ++g) if (n[g]) { out[size_t(g)] = 1; --left; }
    while (left) {
        int best = -1; double most = 0;
        for (int g = 0; g < kWeaponGroups; ++g) if (out[size_t(g)]) { const double each = double(n[g]) / out[size_t(g)]; if (each > most) { most = each; best = g; } }
        if (best < 0) return;
        ++out[size_t(best)]; --left;
    }
}

inline bool Run(const Db& db, const Options& o, ModelParts& out, Report& rep, std::string& why, const Say& say = {}) {
    const double t0 = Now(), deadline = t0 + o.budgetS;
    rep = Report{};
    auto tell = [&](const std::string& s) { if (say) say(s); };
    if (db.rows() < o.minRows) { why = "too little play: " + std::to_string(db.rows()) + " rows, at least " + std::to_string(o.minRows) + " needed"; return false; }
    const Data d(db);
    Pool pool(o.threads); const Cpu cpu(o.forceScalar);
    tell(cpu.avx2 ? "using AVX2" : "no AVX2: plain code, slower");
    // Tokens.
    const int dim = DescribeSize(o.clipMs);
    std::vector<uint32_t> usable; std::vector<float> D; D.reserve(size_t(db.rows()) * size_t(dim));
    std::vector<float> one(static_cast<size_t>(dim));
    for (uint32_t r = 0; r < db.rows(); ++r) if (Describe(d, db.start(r), o.clipMs, one.data())) { usable.push_back(r); D.insert(D.end(), one.begin(), one.end()); }
    if (usable.size() < o.minRows) { why = "too little unbroken play"; return false; }
    const uint32_t K = std::max(2u, std::min(std::min(o.tokens, kMaxTokens), uint32_t(usable.size() / 64)));
    tell("grouping " + std::to_string(usable.size()) + " moments into " + std::to_string(K) + " moves");
    // Each length of weapon has moves of its own, as many as it has play: a move is then never a dagger's and a poleaxe's at once.
    std::array<std::vector<uint32_t>, kWeaponGroups> ofLength;
    for (size_t i = 0; i < usable.size(); ++i) ofLength[WeaponGroupOf(db.row(usable[i]))].push_back(uint32_t(i));
    uint32_t lengths[kWeaponGroups]; for (int g = 0; g < kWeaponGroups; ++g) lengths[g] = uint32_t(ofLength[size_t(g)].size());
    std::array<uint32_t, kWeaponGroups> moves{};
    Allot(lengths, K, moves);
    std::vector<float> C; C.reserve(size_t(K) * size_t(dim));
    std::vector<int> tokenOf(db.rows(), -1);
    std::vector<uint8_t> lengthOf(K, 0);
    std::vector<std::vector<std::pair<float, uint32_t>>> byDist(K);
    uint32_t base = 0;
    for (int g = 0; g < kWeaponGroups; ++g) {
        const std::vector<uint32_t>& rows = ofLength[size_t(g)];
        const uint32_t k = moves[size_t(g)];
        if (!k) continue;
        std::vector<float> part(rows.size() * size_t(dim)), c;
        for (size_t i = 0; i < rows.size(); ++i) std::copy_n(&D[size_t(rows[i]) * size_t(dim)], dim, &part[i * size_t(dim)]);
        const std::vector<int> as = KMeans(pool, cpu, part, rows.size(), dim, int(k), 12, c, o.seed + uint32_t(g), t0 + o.budgetS * .15 * double(base + k) / K);
        for (size_t i = 0; i < rows.size(); ++i) {
            const size_t t = size_t(as[i]), r = usable[rows[i]];
            tokenOf[r] = int(base + t);
            float e = 0;
            for (int j = 0; j < dim; ++j) { const float x = part[i * size_t(dim) + size_t(j)] - c[t * size_t(dim) + size_t(j)]; e += x * x; }
            byDist[base + t].push_back({e, uint32_t(r)});
        }
        C.insert(C.end(), c.begin(), c.end());
        std::fill_n(lengthOf.begin() + base, k, uint8_t(g));
        base += k;
    }
    rep.clusterS = Now() - t0;
    if (o.centres) *o.centres = C;
    D.clear(); D.shrink_to_fit();
    // Inputs and targets.
    std::vector<float> hist = HistoryAtRows(db), events = EventsAtRows(db);
    const std::vector<float> weight = OutcomeAtRows(d, o.outcome, &rep.goodRows, &rep.eventRows);
    if (rep.eventRows) tell("events: " + std::to_string(rep.eventRows) + " moments have them, " + std::to_string(rep.goodRows) + " of those led to a hit landed or more");
    else tell("no fight events in this recording (made before pre6): the nets learn without them");
    auto input = [&](uint32_t r, int k) -> float {
        if (k < kFeatures) return db.row(r)[k];
        if (k < kNetInNoEvents) return hist[size_t(r) * kHistory + size_t(k - kFeatures)];
        return events[size_t(r) * kEventInputs + size_t(k - kNetInNoEvents)];
    };
    std::vector<double> s1(kNetIn, 0.), s2(kNetIn, 0.);
    for (uint32_t r = 0; r < db.rows(); ++r) for (int k = 0; k < kNetIn; ++k) { const double x = input(r, k); s1[size_t(k)] += x; s2[size_t(k)] += x * x; }
    for (size_t k = 0; k < size_t(kNetIn); ++k) { const double n = db.rows(), m = s1[k] / n, v = s2[k] / n - m * m; out.mean[k] = float(m); out.inv[k] = v < 1e-6 || !(o.hears ? (*o.hears)[k] != 0 : NetHears(int(k))) ? 0.f : float(1 / std::sqrt(v)); }   // a thing the player never saw change says nothing
    constexpr int kPadIn = (kNetIn + 15) / 16 * 16;
    std::vector<float> X(size_t(db.rows()) * kPadIn, 0.f), Y(size_t(db.rows()) * kSummary, 0.f);
    for (uint32_t r = 0; r < db.rows(); ++r) {
        float* x = &X[size_t(r) * kPadIn];
        for (int k = 0; k < kNetIn; ++k) x[k] = std::clamp((input(r, k) - out.mean[size_t(k)]) * out.inv[size_t(k)], -kModelInputClamp, kModelInputClamp);
        Next n;
        if (tokenOf[r] >= 0 && Summarise(d, db.start(r), o.summaryMs, n)) {
            float* y = &Y[size_t(r) * kSummary];
            for (int b = 0; b < 6; ++b) y[b] = n.bit[b];
            y[6] = n.turn / 60; y[7] = n.look / 20; y[8] = n.bit[6];
        }
    }
    // The nets, sharing what time is left.
    out.nets.clear();
    const double netStart = Now();
    for (int i = 0; i < o.nets; ++i) {
        Mlp m; m.Init({kNetIn, int(o.hidden), int(o.hidden), int(K) + kSummary}, o.seed + 100 * uint32_t(i));
        const int outW = m.pad.back(), Ki = int(K); const float aux = o.aux;
        const LossFn loss = [&](uint32_t r, const float* y, float* gr) {   // a move that came out well counts more (OutcomeAtRows)
            float l = SoftmaxLoss(y, gr, Ki, outW, tokenOf[r]);
            for (int j = 0; j < kSummary; ++j) { const float e = y[Ki + j] - Y[size_t(r) * kSummary + size_t(j)]; gr[Ki + j] = aux * 2 * e; l += aux * e * e; }
            const float w = weight[r];
            if (w != 1.f) { for (int j = 0; j < Ki + kSummary; ++j) gr[j] *= w; l *= w; }
            return l;
        };
        const double now = Now(), slice = (deadline - now - .02 * o.budgetS) / double(o.nets - i);
        rep.loss = float(Fit(pool, cpu, m, X, usable, loss, {o.epochs, o.batch, o.lr, o.noise, o.seed + 2 + 100 * uint32_t(i), now + slice, &rep.cut}));
        NetParts n; const size_t h = o.hidden, ot = K + kSummary;
        n.w1.resize(size_t(kNetIn) * h); n.w2.resize(h * h); n.w3.resize(h * ot); n.b1.assign(m.B[0].begin(), m.B[0].begin() + long(h)); n.b2.assign(m.B[1].begin(), m.B[1].begin() + long(h)); n.b3.assign(m.B[2].begin(), m.B[2].begin() + long(ot));
        for (size_t a = 0; a < size_t(kNetIn); ++a) std::copy_n(&m.W[0][a * size_t(m.pad[1])], h, &n.w1[a * h]);
        for (size_t a = 0; a < h; ++a) { std::copy_n(&m.W[1][a * size_t(m.pad[2])], h, &n.w2[a * h]); std::copy_n(&m.W[2][a * size_t(m.pad[3])], ot, &n.w3[a * ot]); }
        out.nets.push_back(std::move(n));
        tell("net " + std::to_string(i + 1) + " of " + std::to_string(o.nets) + " done, loss " + std::to_string(rep.loss));
    }
    rep.netS = Now() - netStart;
    // The clips: each token's rows at even steps from its heart to its edge.
    out.name = db.name(); out.kit = db.kit(); out.hidden = o.hidden; out.tokens = K; out.variants = o.variants; out.playedSeconds = float(db.PlayedSeconds());
    out.clips.assign(size_t(K) * o.variants, ClipParts{});
    for (uint32_t t = 0; t < K; ++t) {
        auto& v = byDist[t];
        std::sort(v.begin(), v.end());
        for (uint32_t q = 0; q < o.variants && !v.empty(); ++q) {
            const uint32_t r = v[size_t((double(q) + .5) / o.variants * double(v.size()))].second;
            ClipParts& c = out.clips[size_t(t) * o.variants + q];
            Next n;
            if (!Summarise(d, db.start(r), o.summaryMs, n)) continue;
            std::copy(n.bit, n.bit + 7, c.bit); c.turn = n.turn; c.look = n.look; c.source = db.start(r); c.group = lengthOf[t];
            const float* f = db.row(r);
            c.prevTurn = f[33]; c.prevLook = f[34]; c.foeX = f[17]; c.foeY = f[18];
            for (int k = 0; k < 7; ++k) if (f[35 + k] > .5f) c.held = uint8_t(c.held | 1u << k);
            float held = 0;
            for (uint32_t i = db.start(r); held < o.keepMs && c.samples.size() < kMaxClipSamples; ++i) {
                if (i > d.stretchEnd[db.start(r)]) break;
                c.samples.push_back(db.sample(i)); held += db.sample(i).dtMs;
            }
        }
    }
    rep.rows = db.rows(); rep.usable = uint32_t(usable.size()); rep.tokens = K; rep.seconds = Now() - t0;
    return true;
}

}  // namespace hsm::clone::train
