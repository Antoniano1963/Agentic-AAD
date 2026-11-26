// ============================================================================
// PFSP from-scratch solver  --  DeepSeek_run01 (CPU3 from-scratch experiment)
// ============================================================================
// Written from scratch for this experiment.  No existing PFSP solver code was
// consulted or reused; the only external component is the problem definition
// in initial_materials/PROBLEM.md and the checker's makespan recurrence.
//
// Baseline algorithm (candidate v001):
//   1. NEH constructive heuristic with Taillard-style O(m) insertion probes.
//   2. Iterated greedy: destroy d random jobs, greedily reinsert them, then
//      descend the insertion neighbourhood to a local optimum.
//   3. Constant-temperature simulated-annealing acceptance (Ruiz/Stuetzle
//      style temperature scaling T = tf * avgP / 10).
//   4. Best solution tracked separately; the run is purely anytime and stops
//      on a wall-clock deadline that already reserves an exit margin.
//
// Insertion evaluation uses the classic head/tail decomposition:
//   F[i][k] : completion time of the i-th job of the prefix on machine k
//   Q[i][k] : longest path from "entering the suffix at (i,k)" to the exit
//   makespan(A . x . B) = max_k ( h_x[k] + Q[first(B)][k] )
// where h_x is job x's completion vector when started after prefix A.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using std::string;
using std::vector;
using Clock = std::chrono::steady_clock;
using TP = Clock::time_point;

constexpr long long LINF = (long long)1 << 60;

// --------------------------------------------------------------------- rng
struct RNG {
  uint64_t s0 = 1, s1 = 2, s2 = 3, s3 = 4;
  static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  static inline uint64_t mix(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  void seed(uint64_t v) {
    uint64_t x = v ? v : 0x1234567890abcdefULL;
    s0 = mix(x); s1 = mix(x); s2 = mix(x); s3 = mix(x);
    for (int i = 0; i < 16; ++i) next();
  }
  inline uint64_t next() {
    uint64_t r = rotl(s1 * 5, 7) * 9;
    uint64_t t = s1 << 17;
    s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3; s2 ^= t; s3 = rotl(s3, 45);
    return r;
  }
  inline uint32_t next32() { return (uint32_t)(next() >> 32); }
  inline int nextInt(int k) { return (int)(next32() % (uint32_t)k); }
  inline double nextDouble() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ----------------------------------------------------------------- options
// Compile-time defaults; a candidate's parameters are fixed by its build
// command (see BUILD.md inside each candidate directory).
#ifndef PFSP_DESTROY
#define PFSP_DESTROY 4
#endif
#ifndef PFSP_TEMP
#define PFSP_TEMP 0.4
#endif
#ifndef PFSP_LS_SCOPE
#define PFSP_LS_SCOPE 0
#endif
#ifndef PFSP_FULL_EVERY
#define PFSP_FULL_EVERY 0
#endif
#ifndef PFSP_POLISH
#define PFSP_POLISH 0.0
#endif
#ifndef PFSP_LS_FULLPASS
#define PFSP_LS_FULLPASS 0
#endif
#ifndef PFSP_WIN
#define PFSP_WIN 0
#endif
#ifndef PFSP_BLOCK
#define PFSP_BLOCK 0
#endif
#ifndef PFSP_BLOCK_PERIOD
#define PFSP_BLOCK_PERIOD 4
#endif
#ifndef PFSP_ADAPT
#define PFSP_ADAPT 0.0
#endif
#ifndef PFSP_TEMP_SCALE
#define PFSP_TEMP_SCALE 0.0
#endif
#ifndef PFSP_COOL
#define PFSP_COOL 1.0
#endif
#ifndef PFSP_DMAX
#define PFSP_DMAX 0
#endif
#ifndef PFSP_STALL_FRAC
#define PFSP_STALL_FRAC 0.0
#endif
#ifndef PFSP_STALL
#define PFSP_STALL 0
#endif
#ifndef PFSP_RESTART_D
#define PFSP_RESTART_D 0
#endif

struct Options {
  int d = PFSP_DESTROY;        // destruction size
  int dmax = PFSP_DMAX;        // if > d, destruction size is drawn from [d, dmax]
  double stallFrac = PFSP_STALL_FRAC;  // restart from best after this fraction of the budget without improvement (0=off)
  double cool = PFSP_COOL;     // final/initial temperature ratio (1.0 = constant temperature)
  double tempScale = PFSP_TEMP_SCALE;  // exponent of n/100 applied to the temperature factor
  double rtr = 0.0;            // record-to-record acceptance width as a fraction of makespan (0 = pure SA)
  double adapt = PFSP_ADAPT;   // target acceptance rate for worsening moves (0 = fixed temperature)
  int block = PFSP_BLOCK;      // block size for the block-insertion descent (0 = off)
  int lsFullPass = PFSP_LS_FULLPASS;  // lsScope==1: also run one full pass after the scoped descent
  long long fullEvery = PFSP_FULL_EVERY;  // run a full-neighbourhood descent every k IG iterations (0=off)
  double polish = PFSP_POLISH;  // final full-neighbourhood intensification of the incumbent over this fraction of the budget (0=off)
  int blockPeriod = PFSP_BLOCK_PERIOD;  // run the block descent every k-th IG iteration
  double temp = PFSP_TEMP;     // SA temperature factor
  int accept = 0;              // 0 = SA, 1 = accept only strict improvements
  int construct = 0;           // 0 = reinsert by descending total work, 1 = random
  int lsRandomOrder = 1;       // shuffle job visit order inside each LS pass
  int lsScope = PFSP_LS_SCOPE; // 0 = full LS, 1 = LS restarted from touched jobs only
  long long stallRestart = PFSP_STALL;  // restart after N non-improving IG iterations (0 = off)
  int restartD = PFSP_RESTART_D;  // destruction size for restarts (0 => max(8, n/8))
  double margin = -1.0;        // exit margin override (s)
  int win = PFSP_WIN;         // insertion-scan half-window in positions (0 = full scan)
  int verify = 0;             // dev: assert permutation/cost consistency every iteration
  int logStats = 0;
  const char* tracePath = nullptr;
};

// ------------------------------------------------------------------ solver
struct Solver {
  int n = 0, m = 0;
  vector<int> p;             // n*m, job-major
  vector<long long> psum;    // per-job total work
  double avgP = 0.0;

  vector<int> Fb, Qb;        // tables of the current sequence
  vector<int> F2, Q2;        // tables of the current sequence minus one job
  vector<int> zeros;
  vector<int> rollP, rollQ, rollC;  // rolling prefix/tail/block vectors

  vector<int> cur, work, best, pos, order, part, removed, remPos, tmpBlock, inserted;
  int insertedN = 0;
  vector<char> touched;

  RNG rng;
  Options opt;
  TP deadline;
  bool timeUp = false;
  long long checks = 0;

  // instrumentation
  long long iters = 0, lsPasses = 0, restarts = 0;

  void init(int nn, int mm, const vector<int>& pp) {
    n = nn; m = mm; p = pp;
    psum.assign(n, 0);
    long long tot = 0;
    for (int j = 0; j < n; ++j) {
      long long s = 0;
      for (int k = 0; k < m; ++k) s += (long long)p[(size_t)j * m + k];
      psum[j] = s; tot += s;
    }
    avgP = (n > 0 && m > 0) ? (double)tot / (double)((long long)n * (long long)m) : 1.0;
    if (avgP <= 0.0) avgP = 1.0;
    Fb.assign((size_t)n * m + (size_t)m, 0);
    Qb.assign((size_t)(n + 1) * m, 0);
    F2.assign((size_t)(n + 1) * m, 0);
    Q2.assign((size_t)(n + 1) * m, 0);
    zeros.assign(m, 0);
    rollP.assign(m, 0); rollQ.assign(m, 0); rollC.assign(m, 0);
    cur.assign(n, 0); work.assign(n, 0); best.assign(n, 0);
    pos.assign(n, 0); order.assign(n, 0); part.assign(n, 0);
    removed.assign(n, 0); remPos.assign(n, 0); touched.assign(n, 0); tmpBlock.assign(n, 0);
    inserted.assign(n, 0); insertedN = 0;
  }

  inline bool expired() {
    if ((++checks & 7) == 0 && Clock::now() >= deadline) timeUp = true;
    return timeUp;
  }

  // checker-equivalent makespan (identical recurrence, direct evaluation)
  long long makespan(const int* s, int L) const {
    int fin[1024];
    vector<int> heap;
    int* f = fin;
    if (m > 1024) { heap.assign(m, 0); f = heap.data(); }
    for (int k = 0; k < m; ++k) f[k] = 0;
    for (int i = 0; i < L; ++i) {
      const int* px = &p[(size_t)s[i] * m];
      int last = 0;
      for (int k = 0; k < m; ++k) {
        int v = f[k] > last ? f[k] : last;
        v += px[k];
        last = v; f[k] = v;
      }
    }
    return f[m - 1];
  }

  bool isPerm(const int* s) const {
    vector<char> seen(n, 0);
    for (int i = 0; i < n; ++i) {
      int j = s[i];
      if (j < 0 || j >= n || seen[j]) return false;
      seen[j] = 1;
    }
    return true;
  }

  void buildTables(const int* s, int L) {
    int* F = Fb.data();
    for (int i = 0; i < L; ++i) {
      const int* px = &p[(size_t)s[i] * m];
      int* out = F + (size_t)i * m;
      const int* prev = (i == 0) ? zeros.data() : (out - m);
      int last = 0;
      for (int k = 0; k < m; ++k) {
        int v = prev[k] > last ? prev[k] : last;
        v += px[k];
        out[k] = v; last = v;
      }
    }
    int* Q = Qb.data();
    for (int k = 0; k < m; ++k) Q[(size_t)L * m + k] = 0;
    for (int i = L - 1; i >= 0; --i) {
      const int* px = &p[(size_t)s[i] * m];
      int* out = Q + (size_t)i * m;
      const int* nxt = out + m;
      int run = 0;
      for (int k = m - 1; k >= 0; --k) {
        int v = nxt[k] > run ? nxt[k] : run;
        v += px[k];
        out[k] = v; run = v;
      }
    }
  }

  // tables of the sequence s[0..n-1] with index `skip` removed (length n-1).
  // Only the rows needed by a scan window of half-width W are (re)built.
  void buildShort(const int* s, int skip, int W) {
    const int L = n - 1;
    const int lastF = (W <= 0) ? (L - 1) : std::min(L - 1, skip + W - 1);
    const int prev = 0;
    (void)prev;
    const int* prevRow = (skip == 0) ? zeros.data() : &Fb[(size_t)(skip - 1) * m];
    for (int i = skip; i <= lastF; ++i) {
      const int* px = &p[(size_t)s[i + 1] * m];
      int* out = &F2[(size_t)i * m];
      int last = 0;
      for (int k = 0; k < m; ++k) {
        int v = prevRow[k] > last ? prevRow[k] : last;
        v += px[k];
        out[k] = v; last = v;
      }
      prevRow = out;
    }
    const int firstQ = (W <= 0) ? 0 : std::max(0, skip - W);
    const int* nxt = &Qb[(size_t)(skip + 1) * m];
    for (int i = skip - 1; i >= firstQ; --i) {
      const int* px = &p[(size_t)s[i] * m];
      int* out = &Q2[(size_t)i * m];
      int run = 0;
      for (int k = m - 1; k >= 0; --k) {
        int v = nxt[k] > run ? nxt[k] : run;
        v += px[k];
        out[k] = v; run = v;
      }
      nxt = out;
    }
  }

  // cost of inserting job x at position i of a length-L sequence (tables Fb/Qb)
  inline long long evalInsertFull(int L, int x, int i) const {
    const int* px = &p[(size_t)x * m];
    const int* pre = (i == 0) ? zeros.data() : &Fb[(size_t)(i - 1) * m];
    const int* tail = (i == L) ? zeros.data() : &Qb[(size_t)i * m];
    int run = 0;
    long long c = 0;
    for (int k = 0; k < m; ++k) {
      int v = pre[k] > run ? pre[k] : run;
      v += px[k];
      run = v;
      long long t = (long long)v + (long long)tail[k];
      if (t > c) c = t;
    }
    return c;
  }

  // cost of inserting job x at position i of the sequence with `skip` removed
  inline long long evalInsertSkip(int skip, int x, int i) const {
    const int* px = &p[(size_t)x * m];
    const int L = n - 1;
    const int* pre;
    if (i == 0) pre = zeros.data();
    else {
      int j = i - 1;
      pre = (j < skip) ? &Fb[(size_t)j * m] : &F2[(size_t)j * m];
    }
    const int* tail;
    if (i == L) tail = zeros.data();
    else tail = (i < skip) ? &Q2[(size_t)i * m] : &Qb[(size_t)(i + 1) * m];
    int run = 0;
    long long c = 0;
    for (int k = 0; k < m; ++k) {
      int v = pre[k] > run ? pre[k] : run;
      v += px[k];
      run = v;
      long long t = (long long)v + (long long)tail[k];
      if (t > c) c = t;
    }
    return c;
  }

  inline long long bestInsertionFull(int L, int x, int* bestPos) const {
    long long best = LINF;
    int bp = 0;
    for (int i = 0; i <= L; ++i) {
      long long c = evalInsertFull(L, x, i);
      if (c < best) { best = c; bp = i; }
    }
    *bestPos = bp;
    return best;
  }

  inline long long bestInsertionSkip(int skip, int x, int W, int* bestPos) const {
    const int L = n - 1;
    const int lo = (W <= 0) ? 0 : std::max(0, skip - W);
    const int hi = (W <= 0) ? L : std::min(L, skip + W);
    long long best = LINF;
    int bp = lo;
    for (int i = lo; i <= hi; ++i) {
      long long c = evalInsertSkip(skip, x, i);
      if (c < best) { best = c; bp = i; }
    }
    *bestPos = bp;
    return best;
  }

  // makespan of prefix . x . suffix given the prefix completion vector
  inline long long combine(const int* pre, const int* px, const int* tail) const {
    int run = 0;
    long long c = 0;
    for (int k = 0; k < m; ++k) {
      int v = pre[k] > run ? pre[k] : run;
      v += px[k];
      run = v;
      long long t = (long long)v + (long long)tail[k];
      if (t > c) c = t;
    }
    return c;
  }

  // Exact insertion probe for job x currently at position `at`.
  // Builds no auxiliary table: the prefix vector rolls forward through the right
  // half and the tail vector rolls backward through the left half, so one probe
  // costs O(L*m) instead of O(L*m) table construction + O(L*m) scan.
  // With W > 0 only positions [at-W, at+W] are evaluated (windowed neighbourhood).
  long long probeInsertion(const int* s, int at, int x, int W, long long* out) {
    const int L = n - 1;
    const int lo = (W <= 0) ? 0 : (at - W > 0 ? at - W : 0);
    const int hi = (W <= 0) ? L : (at + W < L ? at + W : L);
    const int* px = &p[(size_t)x * m];
    long long best = LINF;
    // left half: positions lo..at-1, descending, tail of shortened suffix rolls
    int* qv = rollQ.data();
    memcpy(qv, &Qb[(size_t)(at + 1) * m], sizeof(int) * (size_t)m);
    for (int q = at - 1; q >= lo; --q) {
      const int* pq = &p[(size_t)s[q] * m];
      int run = 0;
      for (int k = m - 1; k >= 0; --k) {
        int v = qv[k] > run ? qv[k] : run;
        v += pq[k];
        qv[k] = v; run = v;
      }
      const int* pre = (q == 0) ? zeros.data() : &Fb[(size_t)(q - 1) * m];
      long long c = combine(pre, px, qv);
      if (out) out[q] = c;
      if (c < best) best = c;
    }
    // right half: positions at+1..hi, ascending, prefix vector rolls
    int* pv = rollP.data();
    if (at == 0) memset(pv, 0, sizeof(int) * (size_t)m);
    else memcpy(pv, &Fb[(size_t)(at - 1) * m], sizeof(int) * (size_t)m);
    for (int q = at + 1; q <= hi; ++q) {
      const int* pq = &p[(size_t)s[q] * m];
      int run = 0;
      for (int k = 0; k < m; ++k) {
        int v = pv[k] > run ? pv[k] : run;
        v += pq[k];
        pv[k] = v; run = v;
      }
      const int* tail = (q == L) ? zeros.data() : &Qb[(size_t)(q + 1) * m];
      long long c = combine(pv, px, tail);
      if (out) out[q] = c;
      if (c < best) best = c;
    }
    return best;
  }

  // argmin variant used by the local search
  long long probeBest(const int* s, int at, int x, int W, int* bestPos) {
    const int L = n - 1;
    const int lo = (W <= 0) ? 0 : (at - W > 0 ? at - W : 0);
    const int hi = (W <= 0) ? L : (at + W < L ? at + W : L);
    const int* px = &p[(size_t)x * m];
    long long best = LINF;
    int bp = -1;
    int* qv = rollQ.data();
    memcpy(qv, &Qb[(size_t)(at + 1) * m], sizeof(int) * (size_t)m);
    for (int q = at - 1; q >= lo; --q) {
      const int* pq = &p[(size_t)s[q] * m];
      int run = 0;
      for (int k = m - 1; k >= 0; --k) {
        int v = qv[k] > run ? qv[k] : run;
        v += pq[k];
        qv[k] = v; run = v;
      }
      const int* pre = (q == 0) ? zeros.data() : &Fb[(size_t)(q - 1) * m];
      long long c = combine(pre, px, qv);
      if (c < best) { best = c; bp = q; }
    }
    int* pv = rollP.data();
    if (at == 0) memset(pv, 0, sizeof(int) * (size_t)m);
    else memcpy(pv, &Fb[(size_t)(at - 1) * m], sizeof(int) * (size_t)m);
    for (int q = at + 1; q <= hi; ++q) {
      const int* pq = &p[(size_t)s[q] * m];
      int run = 0;
      for (int k = 0; k < m; ++k) {
        int v = pv[k] > run ? pv[k] : run;
        v += pq[k];
        pv[k] = v; run = v;
      }
      const int* tail = (q == L) ? zeros.data() : &Qb[(size_t)(q + 1) * m];
      long long c = combine(pv, px, tail);
      if (c < best) { best = c; bp = q; }
    }
    *bestPos = bp;
    return best;
  }
  // completion vector of a block of `bl` jobs processed behind `pre`
  inline void blockCompletion(const int* pre, const int* jobs, int bl, int* out) {
    memcpy(out, pre, sizeof(int) * (size_t)m);
    for (int b = 0; b < bl; ++b) {
      const int* px = &p[(size_t)jobs[b] * m];
      int run = 0;
      for (int k = 0; k < m; ++k) {
        int v = out[k] > run ? out[k] : run;
        v += px[k];
        out[k] = v; run = v;
      }
    }
  }

  // Best re-insertion of the length-`bl` block starting at position `at`.
  // Returns the best cost and the target position in the shortened sequence.
  long long probeBlockBest(const int* s, int at, int bl, int* bestPos) {
    const int L = n - bl;
    long long best = LINF;
    int bp = -1;
    int* qv = rollQ.data();
    memcpy(qv, &Qb[(size_t)(at + bl) * m], sizeof(int) * (size_t)m);
    for (int q = at - 1; q >= 0; --q) {
      const int* pq = &p[(size_t)s[q] * m];
      int run = 0;
      for (int k = m - 1; k >= 0; --k) {
        int v = qv[k] > run ? qv[k] : run;
        v += pq[k];
        qv[k] = v; run = v;
      }
      const int* pre = (q == 0) ? zeros.data() : &Fb[(size_t)(q - 1) * m];
      blockCompletion(pre, &s[at], bl, rollC.data());
      long long c = 0;
      for (int k = 0; k < m; ++k) {
        long long t2 = (long long)rollC[(size_t)k] + (long long)qv[k];
        if (t2 > c) c = t2;
      }
      if (c < best) { best = c; bp = q; }
    }
    int* pv = rollP.data();
    if (at == 0) memset(pv, 0, sizeof(int) * (size_t)m);
    else memcpy(pv, &Fb[(size_t)(at - 1) * m], sizeof(int) * (size_t)m);
    for (int q = at + 1; q <= L; ++q) {
      const int* pq = &p[(size_t)s[q - 1 + bl] * m];
      int run = 0;
      for (int k = 0; k < m; ++k) {
        int v = pv[k] > run ? pv[k] : run;
        v += pq[k];
        pv[k] = v; run = v;
      }
      const int* tail = (q == L) ? zeros.data() : &Qb[(size_t)(q + bl) * m];
      blockCompletion(pv, &s[at], bl, rollC.data());
      long long c = 0;
      for (int k = 0; k < m; ++k) {
        long long t2 = (long long)rollC[(size_t)k] + (long long)tail[k];
        if (t2 > c) c = t2;
      }
      if (c < best) { best = c; bp = q; }
    }
    *bestPos = bp;
    return best;
  }
  // apply: move the block s[at..at+bl-1] to position bp of the shortened sequence
  void applyBlockMove(int* s, int at, int bl, int bp) {
    int* tmp = tmpBlock.data();
    int i = 0;
    for (int j = 0; j < bp; ++j) tmp[i++] = (j < at) ? s[j] : s[j + bl];
    for (int b = 0; b < bl; ++b) tmp[i++] = s[at + b];
    for (int j = bp; j <= n - bl - 1; ++j) tmp[i++] = (j < at) ? s[j] : s[j + bl];
    memcpy(s, tmp, sizeof(int) * (size_t)n);
  }

  // descent restricted to the jobs that were just reinserted; optionally one
  // final unconditional full pass over every job
  long long localSearchScoped(int* s, long long cost, int blk) {
    updatePos(s, 0, n - 1);
    bool improved = true;
    while (improved) {
      improved = false;
      for (int t = 0; t < insertedN; ++t) {
        if (expired()) return cost;
        int job = inserted[t];
        int at = pos[job];
        int bp = -1;
        long long c = probeBest(s, at, job, opt.win, &bp);
        if (bp >= 0 && c < cost) {
          eraseInsert(s, at, bp);
          cost = c;
          buildTables(s, n);
          int lo = at < bp ? at : bp, hi = at > bp ? at : bp;
          updatePos(s, lo, hi);
          improved = true;
        }
      }
    }
    if (blk > 0) {
      shuffleOrder(n);
      for (int t = 0; t < n; ++t) {
        if (expired()) return cost;
        int job = order[t];
        int at = pos[job];
        int bp = -1;
        long long c = probeBest(s, at, job, opt.win, &bp);
        if (bp >= 0 && c < cost) {
          eraseInsert(s, at, bp);
          cost = c;
          buildTables(s, n);
          int lo = at < bp ? at : bp, hi = at > bp ? at : bp;
          updatePos(s, lo, hi);
        }
      }
    }
    return cost;
  }
  // block-insertion descent (block size bl >= 2)
  long long blockDescent(int* s, long long cost, int bl) {
    if (bl < 2 || bl >= n) return cost;
    bool improved = true;
    while (improved) {
      improved = false;
      for (int i = 0; i + bl <= n; ++i) {
        if (expired()) { buildTables(s, n); return cost; }
        int bp = -1;
        long long c = probeBlockBest(s, i, bl, &bp);
        if (bp >= 0 && c < cost) {
          applyBlockMove(s, i, bl, bp);
          cost = c;
          buildTables(s, n);
          updatePos(s, 0, n - 1);
          improved = true;
        }
      }
    }
    return cost;
  }
  static void eraseInsert(int* s, int from, int to) {
    int job = s[from];
    if (to < from) {
      for (int t = from; t > to; --t) s[t] = s[t - 1];
    } else if (to > from) {
      for (int t = from; t < to; ++t) s[t] = s[t + 1];
    }
    s[to] = job;
  }

  void updatePos(const int* s, int lo, int hi) {
    for (int i = lo; i <= hi; ++i) pos[s[i]] = i;
  }

  void shuffleOrder(int L) {
    for (int i = 0; i < L; ++i) order[i] = i;
    for (int i = L - 1; i > 0; --i) {
      int j = rng.nextInt(i + 1);
      int t = order[i]; order[i] = order[j]; order[j] = t;
    }
  }

  // iterative-improvement descent on the insertion neighbourhood
  long long localSearch(int* s, long long cost) {
    // tables for `s` are already current (invariant maintained by callers)
    updatePos(s, 0, n - 1);
    if (opt.lsRandomOrder) shuffleOrder(n);
    else for (int i = 0; i < n; ++i) order[i] = i;
    bool improved = true;
    while (improved) {
      improved = false;
      ++lsPasses;
      for (int t = 0; t < n; ++t) {
        if (expired()) return cost;
        int job = order[t];
        int at = pos[job];
        int bp = -1;
        long long c = probeBest(s, at, job, opt.win, &bp);
        if (bp >= 0 && c < cost) {
          eraseInsert(s, at, bp);
          cost = c;
          buildTables(s, n);
          int lo = at < bp ? at : bp, hi = at > bp ? at : bp;
          updatePos(s, lo, hi);
          improved = true;
          if (opt.verify) {
            for (int z = 0; z <= n - 1; ++z) {
              if (pos[s[z]] != z) { fprintf(stderr, "VERIFY pos fail\n"); exit(9); }
            }
          }
        }
      }
    }
    return cost;
  }

  long long neh(int* s) {
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      if (psum[a] != psum[b]) return psum[a] > psum[b];
      return a < b;
    });
    int L = 1;
    s[0] = order[0];
    buildTables(s, 1);
    for (int t = 1; t < n; ++t) {
      int job = order[t];
      int bp = 0;
      bestInsertionFull(L, job, &bp);
      for (int u = L; u > bp; --u) s[u] = s[u - 1];
      s[bp] = job;
      ++L;
      buildTables(s, L);
    }
    return makespan(s, n);
  }

  // remove `cnt` jobs at distinct random positions, reinsert greedily
  long long destroyConstruct(int* s, int cnt) {
    shuffleOrder(n);
    for (int t = 0; t < cnt; ++t) {
      int j = t + rng.nextInt(n - t);
      int tmp = order[t]; order[t] = order[j]; order[j] = tmp;
    }
    // positions to remove, sorted ascending
    for (int t = 0; t < cnt; ++t) remPos[t] = order[t];
    for (int a = 1; a < cnt; ++a) {
      int v = remPos[a], b = a - 1;
      while (b >= 0 && remPos[b] > v) { remPos[b + 1] = remPos[b]; --b; }
      remPos[b + 1] = v;
    }
    for (int t = 0; t < cnt; ++t) removed[t] = s[remPos[t]];
    // build the partial sequence
    int L = 0;
    for (int i = 0, t = 0; i < n; ++i) {
      if (t < cnt && remPos[t] == i) { ++t; continue; }
      part[L++] = s[i];
    }
    // construction order
    if (opt.construct == 0) {
      for (int a = 1; a < cnt; ++a) {
        int v = removed[a], b = a - 1;
        long long kv = psum[v];
        while (b >= 0 && psum[removed[b]] < kv) { removed[b + 1] = removed[b]; --b; }
        removed[b + 1] = v;
      }
    } else {
      for (int a = cnt - 1; a > 0; --a) {
        int j = rng.nextInt(a + 1);
        int t = removed[a]; removed[a] = removed[j]; removed[j] = t;
      }
    }
    buildTables(part.data(), L);
    for (int t = 0; t < cnt; ++t) {
      int job = removed[t];
      int bp = 0;
      bestInsertionFull(L, job, &bp);
      for (int u = L; u > bp; --u) part[u] = part[u - 1];
      part[bp] = job;
      ++L;
      buildTables(part.data(), L);
    }
    for (int i = 0; i < n; ++i) s[i] = part[i];
    for (int t2 = 0; t2 < cnt; ++t2) inserted[t2] = removed[t2];
    insertedN = cnt;
    return makespan(s, n);
  }
};

// ---------------------------------------------------------------- helpers
long long naiveMakespan(int n, int m, const vector<int>& p, const vector<int>& s) {
  if (n == 0) return 0;
  vector<vector<long long>> C(n, vector<long long>(m, 0));
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < m; ++k) {
      long long up = i > 0 ? C[i - 1][k] : 0;
      long long left = k > 0 ? C[i][k - 1] : 0;
      C[i][k] = std::max(up, left) + p[(size_t)s[i] * m + k];
    }
  }
  return C[n - 1][m - 1];
}

// ---------------------------------------------------------------- selftest
int runSelftest() {
  RNG rng;
  rng.seed(12345);
  int fails = 0;
  auto fail = [&](const char* msg) { printf("FAIL %s\n", msg); ++fails; };

  for (int trial = 0; trial < 300; ++trial) {
    int n = 1 + rng.nextInt(9);
    int m = 1 + rng.nextInt(5);
    vector<int> p((size_t)n * m);
    for (auto& v : p) v = rng.nextInt(10);
    Solver S;
    S.init(n, m, p);
    vector<int> s(n);
    for (int i = 0; i < n; ++i) s[i] = i;
    for (int i = n - 1; i > 0; --i) { int j = rng.nextInt(i + 1); std::swap(s[i], s[j]); }

    if (S.makespan(s.data(), n) != naiveMakespan(n, m, p, s)) fail("makespan");

    // full-sequence insertion probes
    S.buildTables(s.data(), n);
    for (int x = 0; x < n; ++x) {
      for (int i = 0; i <= n; ++i) {
        vector<int> t(s);
        t.insert(t.begin() + i, x);
        long long want = naiveMakespan(n + 1, m, p, t);
        long long got = S.evalInsertFull(n, x, i);
        if (got != want) { printf("FAIL ins n=%d m=%d x=%d i=%d %lld vs %lld\n", n, m, x, i, got, want); ++fails; }
        if (fails > 20) return 1;
      }
      int bp = -1;
      long long bestv = S.bestInsertionFull(n, x, &bp);
      long long bf = LINF;
      for (int i = 0; i <= n; ++i) {
        vector<int> t(s);
        t.insert(t.begin() + i, x);
        bf = std::min(bf, naiveMakespan(n + 1, m, p, t));
      }
      if (bestv != bf) { printf("FAIL ins-best %lld vs %lld\n", bestv, bf); ++fails; }
    }
    // shortened-sequence probes
    if (n >= 2) {
      for (int skip = 0; skip < n; ++skip) {
        vector<int> shortSeq;
        for (int i = 0; i < n; ++i) if (i != skip) shortSeq.push_back(s[i]);
        S.buildTables(s.data(), n);
        S.buildShort(s.data(), skip, 0);
        for (int x = 0; x < n; ++x) {
          for (int i = 0; i <= n - 1; ++i) {
            vector<int> t(shortSeq);
            t.insert(t.begin() + i, x);
            long long want = naiveMakespan(n, m, p, t);
            long long got = S.evalInsertSkip(skip, x, i);
            if (got != want) {
              printf("FAIL skip n=%d m=%d sk=%d x=%d i=%d %lld vs %lld\n", n, m, skip, x, i, got, want);
              ++fails;
            }
          }
        }
      }
    }
  }

  // block probe: best block re-insertion must match brute force
  for (int trial = 0; trial < 120; ++trial) {
    int n = 4 + rng.nextInt(10);
    int m = 1 + rng.nextInt(4);
    vector<int> p((size_t)n * m);
    for (auto& v : p) v = 1 + rng.nextInt(12);
    Solver S;
    S.init(n, m, p);
    vector<int> s(n);
    for (int i = 0; i < n; ++i) s[i] = i;
    for (int i = n - 1; i > 0; --i) { int j = rng.nextInt(i + 1); std::swap(s[i], s[j]); }
    S.buildTables(s.data(), n);
    for (int bl = 2; bl <= 3; ++bl) {
      for (int at = 0; at + bl <= n; ++at) {
        vector<int> rest;
        for (int i = 0; i < n; ++i) if (i < at || i >= at + bl) rest.push_back(s[i]);
        long long want = LINF;
        for (int q = 0; q <= n - bl; ++q) {
          if (q == at) continue;  // the probe deliberately skips the no-op position
          vector<int> t(rest);
          t.insert(t.begin() + q, s.begin() + at, s.begin() + at + bl);
          want = std::min(want, naiveMakespan(n, m, p, t));
        }
        int bp = -1;
        long long got = S.probeBlockBest(s.data(), at, bl, &bp);
        if (got != want) {
          printf("FAIL blk n=%d m=%d at=%d bl=%d got=%lld want=%lld\n", n, m, at, bl, got, want);
          ++fails;
        }
        if (fails > 20) return 1;
      }
    }
  }
  // rolling probe: per-position costs must match brute force exactly
  for (int trial = 0; trial < 200; ++trial) {
    int n = 1 + rng.nextInt(12);
    int m = 1 + rng.nextInt(5);
    vector<int> p((size_t)n * m);
    for (auto& v : p) v = rng.nextInt(15);
    Solver S;
    S.init(n, m, p);
    vector<int> s(n);
    for (int i = 0; i < n; ++i) s[i] = i;
    for (int i = n - 1; i > 0; --i) { int j = rng.nextInt(i + 1); std::swap(s[i], s[j]); }
    S.buildTables(s.data(), n);
    vector<long long> costs(n + 1, -1);
    for (int at = 0; at < n; ++at) {
      int x = s[at];
      std::fill(costs.begin(), costs.end(), -1);
      S.probeInsertion(s.data(), at, x, 0, costs.data());
      for (int q = 0; q <= n - 1; ++q) {
        if (q == at) continue;
        vector<int> shortSeq;
        for (int i = 0; i < n; ++i) if (i != at) shortSeq.push_back(s[i]);
        vector<int> t(shortSeq);
        t.insert(t.begin() + q, x);
        long long want = naiveMakespan(n, m, p, t);
        if (costs[q] != want) {
          printf("FAIL roll n=%d m=%d at=%d q=%d got=%lld want=%lld\n", n, m, at, q, costs[q], want);
          ++fails;
        }
      }
      int bp = -1;
      long long got = S.probeBest(s.data(), at, x, 0, &bp);
      long long want = LINF;
      for (int q = 0; q <= n - 1; ++q) if (q != at) want = std::min(want, costs[q]);
      if (n >= 2 && (got != want || costs[bp] != got)) {
        printf("FAIL rollbest n=%d m=%d at=%d got=%lld want=%lld bp=%d\n", n, m, at, got, want, bp);
        ++fails;
      }
      if (fails > 20) return 1;
    }
  }
  // windowed insertion probes: the scan must return the true best inside the window
  for (int trial = 0; trial < 200; ++trial) {
    int n = 2 + rng.nextInt(12);
    int m = 1 + rng.nextInt(5);
    vector<int> p((size_t)n * m);
    for (auto& v : p) v = 1 + rng.nextInt(20);
    Solver S;
    S.init(n, m, p);
    vector<int> s(n);
    for (int i = 0; i < n; ++i) s[i] = i;
    for (int i = n - 1; i > 0; --i) { int j = rng.nextInt(i + 1); std::swap(s[i], s[j]); }
    S.buildTables(s.data(), n);
    for (int skip = 0; skip < n; ++skip) {
      vector<int> shortSeq;
      for (int i = 0; i < n; ++i) if (i != skip) shortSeq.push_back(s[i]);
      int x = s[skip];
      for (int W = 1; W <= n + 2; W += 3) {
        S.buildShort(s.data(), skip, W);
        int bp = -1;
        long long got = S.bestInsertionSkip(skip, x, W, &bp);
        int lo = std::max(0, skip - W), hi = std::min(n - 1, skip + W);
        long long want = LINF;
        for (int i = lo; i <= hi; ++i) {
          vector<int> t(shortSeq);
          t.insert(t.begin() + i, x);
          want = std::min(want, naiveMakespan(n, m, p, t));
        }
        if (got != want) {
          printf("FAIL win n=%d m=%d sk=%d W=%d got=%lld want=%lld\n", n, m, skip, W, got, want);
          ++fails;
        }
      }
    }
    if (fails > 20) return 1;
  }
  // integrated smoke test: NEH, LS, destroy-construct must stay consistent
  for (int trial = 0; trial < 50; ++trial) {
    int n = 4 + rng.nextInt(30);
    int m = 2 + rng.nextInt(8);
    vector<int> p((size_t)n * m);
    for (auto& v : p) v = 1 + rng.nextInt(50);
    Solver S;
    S.init(n, m, p);
    S.rng.seed(0xBEEF + (uint64_t)trial);
    S.deadline = Clock::now() + std::chrono::milliseconds(500);
    vector<int> s(n);
    long long c = S.neh(s.data());
    if (c != naiveMakespan(n, m, p, s)) fail("neh cost");
    long long c2 = S.localSearch(s.data(), c);
    if (c2 != naiveMakespan(n, m, p, s)) fail("ls cost");
    if (c2 > c) fail("ls worsened");
    auto validPerm = [&](const vector<int>& v) {
      vector<char> seen(n, 0);
      for (int i = 0; i < n; ++i) {
        if (v[i] < 0 || v[i] >= n || seen[v[i]]) return false;
        seen[v[i]] = 1;
      }
      return true;
    };
    if (!validPerm(s)) fail("ls permutation");
    for (int rep = 0; rep < 15; ++rep) {
      long long d = S.destroyConstruct(s.data(), std::min(4, n - 1));
      if (d != naiveMakespan(n, m, p, s)) fail("dc cost");
      if (!validPerm(s)) fail("dc permutation");
      long long c3 = S.localSearch(s.data(), d);
      if (c3 != naiveMakespan(n, m, p, s)) fail("ls2 cost");
      if (c3 > d) fail("ls2 worsened");
      if (fails > 20) break;
    }
    if (fails > 20) break;
  }
  if (fails == 0) printf("SELFTEST OK\n");
  else printf("SELFTEST FAILURES: %d\n", fails);
  return fails == 0 ? 0 : 1;
}
// ---------------------------------------------------------------- instance
bool readInstance(const char* path, int& n, int& m, vector<int>& p) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return false; }
  fseek(f, 0, SEEK_SET);
  vector<char> buf((size_t)sz + 1, 0);
  size_t rd = fread(buf.data(), 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = 0;
  const char* q = buf.data();
  const char* end = buf.data() + rd;
  auto nextInt = [&](long long& out) -> bool {
    while (q < end && (*q < '0' || *q > '9') && *q != '-') ++q;
    if (q >= end) return false;
    bool neg = false;
    if (*q == '-') { neg = true; ++q; }
    long long v = 0;
    while (q < end && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); ++q; }
    out = neg ? -v : v;
    return true;
  };
  long long vn = 0, vm = 0;
  if (!nextInt(vn) || !nextInt(vm)) return false;
  n = (int)vn; m = (int)vm;
  if (n <= 0 || m <= 0) return false;
  p.assign((size_t)n * m, 0);
  vector<int> row(m, 0);
  for (int j = 0; j < n; ++j) {
    for (int t = 0; t < m; ++t) {
      long long id = 0, val = 0;
      if (!nextInt(id) || !nextInt(val)) return false;
      if (id < 0 || id >= m) return false;
      row[(size_t)id] = (int)val;
    }
    for (int k = 0; k < m; ++k) p[(size_t)j * m + k] = row[k];
  }
  return true;
}

bool writeSolution(const char* pathOut, const vector<int>& seq, long long ms) {
  string tmp = string(pathOut) + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  string s;
  s.reserve((size_t)seq.size() * 5 + 64);
  char num[32];
  s += "{\"permutation\": [";
  for (size_t i = 0; i < seq.size(); ++i) {
    if (i) s += ',';
    snprintf(num, sizeof(num), "%d", seq[i]);
    s += num;
  }
  s += "], \"makespan\": ";
  snprintf(num, sizeof(num), "%lld", ms);
  s += num;
  s += "}\n";
  size_t wr = fwrite(s.data(), 1, s.size(), f);
  fclose(f);
  if (wr != s.size()) return false;
  return rename(tmp.c_str(), pathOut) == 0;
}

}  // namespace

// -------------------------------------------------------------------- main
int main(int argc, char** argv) {
  string instPath, outPath, tracePath;
  double timeLimit = 0.0;
  uint64_t seed = 0;
  Options opt;
  bool selftest = false;
  for (int i = 1; i < argc; ++i) {
    string a = argv[i];
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", name); exit(2); }
      return argv[++i];
    };
    if (a == "--instance") instPath = need("--instance");
    else if (a == "--output") outPath = need("--output");
    else if (a == "--time-limit") timeLimit = atof(need("--time-limit"));
    else if (a == "--seed") seed = strtoull(need("--seed"), nullptr, 10);
    else if (a == "--d") opt.d = atoi(need("--d"));
    else if (a == "--dmax") opt.dmax = atoi(need("--dmax"));
    else if (a == "--stall-frac") opt.stallFrac = atof(need("--stall-frac"));
    else if (a == "--cool") opt.cool = atof(need("--cool"));
    else if (a == "--temp-scale") opt.tempScale = atof(need("--temp-scale"));
    else if (a == "--rtr") opt.rtr = atof(need("--rtr"));
    else if (a == "--adapt") opt.adapt = atof(need("--adapt"));
    else if (a == "--polish") opt.polish = atof(need("--polish"));
    else if (a == "--full-every") opt.fullEvery = atoll(need("--full-every"));
    else if (a == "--block") opt.block = atoi(need("--block"));
    else if (a == "--block-period") opt.blockPeriod = atoi(need("--block-period"));
    else if (a == "--temp") opt.temp = atof(need("--temp"));
    else if (a == "--accept") {
      string v = need("--accept");
      opt.accept = (v == "better") ? 1 : 0;
    } else if (a == "--construct") {
      string v = need("--construct");
      opt.construct = (v == "rand" || v == "random") ? 1 : 0;
    } else if (a == "--ls-order") {
      string v = need("--ls-order");
      opt.lsRandomOrder = (v == "seq") ? 0 : 1;
    } else if (a == "--ls-scope") {
      string v = need("--ls-scope");
      opt.lsScope = (v == "touched") ? 1 : 0;
    } else if (a == "--stall-restart") opt.stallRestart = atoll(need("--stall-restart"));
    else if (a == "--restart-d") opt.restartD = atoi(need("--restart-d"));
    else if (a == "--margin") opt.margin = atof(need("--margin"));
    else if (a == "--stats") opt.logStats = 1;
    else if (a == "--verify") opt.verify = 1;
    else if (a == "--win") opt.win = atoi(need("--win"));
    else if (a == "--trace") tracePath = need("--trace");
    else if (a == "--selftest") selftest = true;
    else if (a == "--version") { printf("solver v001\n"); return 0; }
    else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }
  if (!tracePath.empty()) opt.tracePath = tracePath.c_str();
  if (selftest) return runSelftest();
  if (instPath.empty() || outPath.empty() || timeLimit <= 0.0) {
    fprintf(stderr, "usage: --instance P --output P --time-limit S [--seed N]\n");
    return 2;
  }
  const TP t0 = Clock::now();

  int n = 0, m = 0;
  vector<int> p;
  if (!readInstance(instPath.c_str(), n, m, p)) {
    fprintf(stderr, "cannot read instance %s\n", instPath.c_str());
    return 3;
  }

  Solver S;
  S.init(n, m, p);
  S.opt = opt;
  S.rng.seed(seed);

  double margin = opt.margin > 0.0 ? opt.margin : (0.50 + 0.005 * timeLimit);
  if (margin > 2.5) margin = 2.5;
  double usable = timeLimit - margin;
  if (usable < 0.02) usable = 0.02;
  S.deadline = t0 + std::chrono::microseconds((long long)(usable * 1e6));

  vector<int>& cur = S.cur;
  long long curCost = S.neh(cur.data());
  long long bestCost = curCost;
  S.best = cur;

  int d = opt.d;
  if (d > n - 1) d = n - 1;
  if (d < 0) d = 0;
  long double temp = (long double)opt.temp * (long double)S.avgP / 10.0L;
  if (opt.tempScale > 0.0) {
    temp *= pow((double)n / 100.0, opt.tempScale);
  }
  if (temp < 1e-9L) temp = 1e-9L;

  long long stall = 0;
  double tAdj = (double)temp;
  long long adaptN = 0, adaptAcc = 0;
  TP lastBest = t0;
  FILE* trace = nullptr;
  TP traceNext = t0;
  double traceStep = usable / 100.0;
  if (traceStep < 0.2) traceStep = 0.2;
  if (opt.tracePath && opt.tracePath[0]) {
    trace = fopen(opt.tracePath, "w");
    if (trace) fprintf(trace, "iter,elapsed_ms,cur,best\n");
  }

  // main IG loop
  bool polished = false;
  while (d > 0 && d < n) {
    if (Clock::now() >= S.deadline) break;
    if (!polished && opt.polish > 0.0) {
      double prog = std::chrono::duration<double>(Clock::now() - t0).count() / usable;
      if (prog > 1.0 - opt.polish) {
        polished = true;
        int savedWin = opt.win;
        opt.win = 0;
        S.opt.win = 0;
        S.work = S.best;
        S.buildTables(S.work.data(), n);  // tables must describe `work` before the descent
        long long c = S.localSearch(S.work.data(), bestCost);
        if (opt.verify) {
          long long ms = S.makespan(S.work.data(), n);
          if (!S.isPerm(S.work.data()) || ms != c) {
            fprintf(stderr, "VERIFY polish fail ms=%lld cost=%lld\n", ms, c);
            return 9;
          }
        }
        if (c < bestCost) { bestCost = c; S.best = S.work; }
        opt.win = savedWin;
        S.opt.win = savedWin;
        S.cur = S.best;
        curCost = bestCost;
      }
    }
    int cnt = d;
    if (opt.dmax > d) cnt = d + S.rng.nextInt(opt.dmax - d + 1);
    if (cnt > n - 1) cnt = n - 1;
    S.work = S.cur;  // work must start from the current solution
    long long newCost = S.destroyConstruct(S.work.data(), cnt);
    if (opt.lsScope == 1) newCost = S.localSearchScoped(S.work.data(), newCost, opt.lsFullPass);
    else newCost = S.localSearch(S.work.data(), newCost);
    if (opt.fullEvery > 0 && opt.win > 0 && (S.iters % opt.fullEvery) == 0) {
      S.opt.win = 0;
      newCost = S.localSearch(S.work.data(), newCost);
      S.opt.win = opt.win;
      if (opt.verify) {
        long long ms = S.makespan(S.work.data(), n);
        if (!S.isPerm(S.work.data()) || ms != newCost) {
          fprintf(stderr, "VERIFY fullpass fail ms=%lld cost=%lld\n", ms, newCost);
          return 9;
        }
      }
    }
    if (opt.block >= 2 && (S.iters % (long long)opt.blockPeriod) == 0) {
      newCost = S.blockDescent(S.work.data(), newCost, opt.block);
      if (opt.verify) {
        long long ms = S.makespan(S.work.data(), n);
        if (!S.isPerm(S.work.data()) || ms != newCost) {
          fprintf(stderr, "VERIFY block fail ms=%lld cost=%lld\n", ms, newCost);
          return 9;
        }
      }
    }
    ++S.iters;
    if (opt.verify) {
      long long ms = S.makespan(S.work.data(), n);
      if (!S.isPerm(S.work.data()) || ms != newCost) {
        fprintf(stderr, "VERIFY fail iter=%lld perm=%d ms=%lld cost=%lld\n", S.iters,
                (int)S.isPerm(S.work.data()), ms, newCost);
        return 9;
      }
    }
    if (newCost < bestCost) {
      bestCost = newCost;
      S.best = S.work;
      stall = 0;
      lastBest = Clock::now();
    } else {
      ++stall;
    }
    const bool improving = newCost < curCost;
    double delta = (double)(newCost - curCost);
    bool acc = improving;
    double tcur = (opt.adapt > 0.0) ? tAdj : (double)temp;
    if (!acc && opt.rtr > 0.0) acc = delta < opt.rtr * (double)bestCost;
    if (!acc && opt.accept == 0) {
      if (opt.adapt <= 0.0 && opt.cool != 1.0) {
        double prog = std::chrono::duration<double>(Clock::now() - t0).count() / usable;
        if (prog > 1.0) prog = 1.0;
        tcur = (double)temp * (1.0 - prog + prog * opt.cool);
      }
      if (tcur < 1e-9) tcur = 1e-9;
      acc = S.rng.nextDouble() < exp(-delta / tcur);
    }
    if (opt.adapt > 0.0 && !improving) {
      ++adaptN;
      if (acc) ++adaptAcc;
      if (adaptN >= 128) {
        double rate = (double)adaptAcc / (double)adaptN;
        tAdj *= exp(0.6 * (rate - opt.adapt));
        double lo = 0.2 * (double)temp, hi = 6.0 * (double)temp;
        if (tAdj < lo) tAdj = lo;
        if (tAdj > hi) tAdj = hi;
        adaptN = 0; adaptAcc = 0;
      }
    }
    if (acc) {
      curCost = newCost;
      S.cur.swap(S.work);
    }
    if (trace && std::chrono::duration<double>(Clock::now() - traceNext).count() > 0.0) {
      double el = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
      fprintf(trace, "%lld,%.1f,%lld,%lld\n", S.iters, el, curCost, bestCost);
      traceNext = Clock::now() + std::chrono::microseconds((long long)(traceStep * 1e6));
    }
    bool timeRestart = false;
    if (opt.stallFrac > 0.0) {
      double since = std::chrono::duration<double>(Clock::now() - lastBest).count();
      if (since > opt.stallFrac * usable) timeRestart = true;
    }
    if ((timeRestart || (opt.stallRestart > 0 && stall >= opt.stallRestart)) && Clock::now() < S.deadline) {
      // strong restart: from the incumbent, destroy a large slice and rebuild
      S.cur = S.best;
      curCost = bestCost;
      int rd = opt.restartD > 0 ? opt.restartD : (n / 8 > 8 ? n / 8 : 8);
      if (rd > n - 1) rd = n - 1;
      S.work = S.cur;
      long long c = S.destroyConstruct(S.work.data(), rd);
      c = S.localSearch(S.work.data(), c);
      if (c < bestCost) { bestCost = c; S.best = S.work; }
      S.cur.swap(S.work);
      curCost = c;
      ++S.restarts;
      stall = 0;
      lastBest = Clock::now();
    }
  }
  if (trace) { fclose(trace); trace = nullptr; }

  // final answer: recompute independently with the direct recurrence
  long long finalCost = S.makespan(S.best.data(), n);  // always the independent recomputation
  if (finalCost != bestCost && opt.logStats) {
    fprintf(stderr, "warning: tracked cost %lld differs from recomputed makespan %lld\n", bestCost, finalCost);
  }

  double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
  if (opt.logStats) {
    fprintf(stderr,
            "stats n=%d m=%d iters=%lld ls_pass=%lld restarts=%lld best=%lld elapsed=%.3f limit=%.1f\n",
            n, m, S.iters, S.lsPasses, S.restarts, finalCost, elapsed, timeLimit);
  }
  if (!writeSolution(outPath.c_str(), S.best, finalCost)) {
    fprintf(stderr, "cannot write %s\n", outPath.c_str());
    return 4;
  }
  return 0;
}
