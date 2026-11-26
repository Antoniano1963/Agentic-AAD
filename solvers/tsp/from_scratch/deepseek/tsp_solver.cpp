// tsp_solver.cpp -- v001
//
// From-scratch symmetric Euclidean TSP heuristic (TSPLIB TYPE:TSP / EUC_2D).
// No third-party TSP code, no known tours, no reference optimum is read: the
// TSPLIB parser stops hard at the EOF marker.
//
// Pipeline
//   1. parse TSPLIB instance
//   2. exact K-nearest-neighbour candidate lists (O(n^2) pairwise scan)
//   3. initial tour: nearest-neighbour construction
//   4. local search: 2-opt + Or-opt(1..3), neighbour lists + don't-look bits
//   5. iterated local search: double-bridge perturbation + local search,
//      strict-improvement acceptance against the current local optimum
//
// Single threaded, allocation-light: hundreds of concurrent solves stay cheap.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point T0;
inline double elapsedSec() {
  return std::chrono::duration<double>(Clock::now() - T0).count();
}

// ---------------------------------------------------------------- instance --
int N = 0;
int K = 10;                       // candidate-list size
std::vector<double> PX, PY;       // coordinates
std::vector<long long> OID;       // original TSPLIB city identifiers
std::vector<int> NI;              // N*K nearest-neighbour indices
std::vector<int> ND;              // N*K nearest-neighbour distances
std::vector<int> T;               // tour: T[pos] = city
std::vector<int> POS;             // POS[city] = position in T
long long CUR_LEN = 0;            // length of T, maintained by exact move gains

std::vector<int> QS;              // work stack backing the don't-look bits
std::vector<unsigned char> INQ;

double HardDeadline = 1e18;       // seconds since T0
bool opt3opt = true;              // enable the 3-opt move set (A/B switch)
bool opt3optCase8 = false;        // A C^R B^R D: not guided by the enumeration
bool opt3optExtra = false;        // also try 3-opt cases 6 and 7
bool acceptEq = true;            // accept equal-length local optima (plateau walk)
int kickCount = 1;                // double-bridge perturbations per ILS iteration
double rrtDev = 0.0;              // record-to-record travel deviation (0 = off)
int kickBias = 1;                 // 0 = uniform cut points, 1 = bias to long edges
int kickBiasSamples = 3;          // tour edges sampled when picking a biased cut
int oroptMaxL = 3;                // longest Or-opt segment length
int opt3optRoles = 2;             // 3-opt outer loops: 1 = half the cost
int opt3optInner = 0;             // 3-opt inner breadth; 0 means K
bool opt3optCase4 = false;         // enable the 3-opt A B^R C^R D reconnection
int oroptAnchors = 2;             // Or-opt anchor edges tried per candidate (1 or 2)

inline int distc(int i, int j) {
  const double dx = PX[i] - PX[j];
  const double dy = PY[i] - PY[j];
  return (int)(std::sqrt(dx * dx + dy * dy) + 0.5);
}

inline int nxtCity(int c) {
  const int p = POS[c] + 1;
  return T[p == N ? 0 : p];
}
inline int prvCity(int c) {
  const int p = POS[c] - 1;
  return T[p < 0 ? N - 1 : p];
}
inline void pushCity(int c) {
  if (!INQ[c]) {
    INQ[c] = 1;
    QS.push_back(c);
  }
}
inline void rebuildPos() {
  for (int i = 0; i < N; ++i) POS[T[i]] = i;
}

long long tourLength() {
  long long s = 0;
  for (int i = 0; i < N; ++i) s += distc(T[i], T[(i + 1 == N) ? 0 : i + 1]);
  return s;
}

#ifdef TSP_DEBUG
// Opt-in (TSP_DBG_VERIFY=1) per-move validation: permutation, POS agreement and
// exact length accounting.  Only ever compiled into the development binary.
inline bool dbgEnabled() {
  static const bool e = (std::getenv("TSP_DBG_VERIFY") != nullptr);
  return e;
}
void dbgValidate(const char* what) {
  if (!dbgEnabled()) return;
  std::vector<unsigned char> seen((size_t)N, 0);
  for (int i = 0; i < N; ++i) {
    const int c = T[i];
    if (c < 0 || c >= N || seen[c] || POS[c] != i) {
      std::fprintf(stderr, "CORRUPT TOUR after %s at i=%d\n", what, i);
      std::exit(8);
    }
    seen[c] = 1;
  }
  const long long rc = tourLength();
  if (rc != CUR_LEN) {
    std::fprintf(stderr, "DRIFT after %s: tracked %lld recount %lld\n", what, CUR_LEN, rc);
    std::exit(7);
  }
}
#endif

// ------------------------------------------------------------------ parser --
bool parseInstance(const char* path) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  std::string buf;
  {
    char tmp[1 << 16];
    size_t got;
    while ((got = std::fread(tmp, 1, sizeof(tmp), f)) > 0) buf.append(tmp, got);
  }
  std::fclose(f);
  if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
      (unsigned char)buf[2] == 0xBF)
    buf.erase(0, 3);

  bool inCoords = false;
  int declared = -1;
  std::vector<long long> ids;
  ids.reserve(1 << 15);
  PX.reserve(1 << 15);
  PY.reserve(1 << 15);

  size_t i = 0;
  const size_t n = buf.size();
  while (i < n) {
    size_t j = i;
    while (j < n && buf[j] != '\n') ++j;
    size_t a = i, b = j;
    while (a < b && (buf[a] == ' ' || buf[a] == '\t' || buf[a] == '\r')) ++a;
    while (b > a && (buf[b - 1] == ' ' || buf[b - 1] == '\t' || buf[b - 1] == '\r')) --b;
    if (b > a) {
      const char* s = buf.data() + a;
      const size_t len = b - a;
      if (inCoords) {
        if (len >= 3 && s[0] == 'E' && s[1] == 'O' && s[2] == 'F') break;  // hard stop
        char* endp = nullptr;
        const long long id = std::strtoll(s, &endp, 10);
        if (endp == s) {
          i = j + 1;
          continue;
        }
        const double x = std::strtod(endp, &endp);
        const double y = std::strtod(endp, &endp);
        ids.push_back(id);
        PX.push_back(x);
        PY.push_back(y);
      } else {
        const char* colon = nullptr;
        for (size_t k = 0; k + 1 < len; ++k)
          if (s[k] == ':') {
            colon = s + k;
            break;
          }
        if (colon) {
          size_t klen = (size_t)(colon - s);
          while (klen > 0 && (s[klen - 1] == ' ' || s[klen - 1] == '\t')) --klen;
          if (klen == 9 && std::strncmp(s, "DIMENSION", 9) == 0) {
            declared = std::atoi(colon + 1);
          } else if (klen == 16 && std::strncmp(s, "EDGE_WEIGHT_TYPE", 16) == 0) {
            const char* v = colon + 1;
            while (*v == ' ' || *v == '\t') ++v;
            if (std::strncmp(v, "EUC_2D", 6) != 0) return false;
          }
        } else if (len == 18 && std::strncmp(s, "NODE_COORD_SECTION", 18) == 0) {
          inCoords = true;
        }
      }
    }
    i = j + 1;
  }

  N = (int)PX.size();
  if (N < 2) return false;
  if (declared >= 0 && declared != N) return false;
  OID = ids;
  return true;
}

// -------------------------------------------------------- candidate lists ---
void buildNeighbors(int k) {
  if (k > N - 1) k = N - 1;
  if (k < 1) k = 1;
  K = k;
  NI.assign((size_t)N * K, 0);
  ND.assign((size_t)N * K, 0);
  const int INF = INT32_MAX;
  for (size_t t = 0; t < (size_t)N * K; ++t) {
    NI[t] = -1;
    ND[t] = INF;
  }
  for (int i = 0; i < N; ++i) {
    int* ni = &NI[(size_t)i * K];
    int* nd = &ND[(size_t)i * K];
    const double xi = PX[i], yi = PY[i];
    for (int j = i + 1; j < N; ++j) {
      const double dx = xi - PX[j], dy = yi - PY[j];
      const int dd = (int)(std::sqrt(dx * dx + dy * dy) + 0.5);
      if (dd < nd[K - 1]) {
        int t = K - 1;
        while (t > 0 && nd[t - 1] > dd) {
          nd[t] = nd[t - 1];
          ni[t] = ni[t - 1];
          --t;
        }
        nd[t] = dd;
        ni[t] = j;
      }
      int* nj = &NI[(size_t)j * K];
      int* ndj = &ND[(size_t)j * K];
      if (dd < ndj[K - 1]) {
        int t = K - 1;
        while (t > 0 && ndj[t - 1] > dd) {
          ndj[t] = ndj[t - 1];
          nj[t] = nj[t - 1];
          --t;
        }
        ndj[t] = dd;
        nj[t] = i;
      }
    }
  }
}

// ---------------------------------------------------------------- 2-opt -----
// Reverse tour positions p1..p2 walking forward (cyclic).  For an undirected
// tour reversing the complement is equivalent, so always take the short side.
void flipRange(int p1, int p2) {
  int len = (p2 - p1 + N) % N + 1;
  if (len > N / 2) {
    const int a = (p2 + 1) % N;
    const int b = (p1 - 1 + N) % N;
    const int nl = N - len;
    if (nl <= 0) return;
    p1 = a;
    p2 = b;
    len = nl;
  }
  for (int k = 0; k < len / 2; ++k) {
    int i = p1 + k;
    if (i >= N) i -= N;
    int j = p2 - k;
    if (j < 0) j += N;
    const int ci = T[i], cj = T[j];
    T[i] = cj;
    T[j] = ci;
    POS[cj] = i;
    POS[ci] = j;
  }
}

bool try2opt(int a) {
  const int* ni = &NI[(size_t)a * K];
  const int* nd = &ND[(size_t)a * K];
  for (int dir = 0; dir < 2; ++dir) {
    const int b = dir ? prvCity(a) : nxtCity(a);
    if (b == a) continue;
    const int dab = distc(a, b);
    for (int k = 0; k < K; ++k) {
      const int c = ni[k];
      if (c == a || c == b) continue;
      const int d = dir ? prvCity(c) : nxtCity(c);
      if (d == a || d == b) continue;
      const int dac = nd[k];
      const int dcd = distc(c, d);
      const int dbd = distc(b, d);
      const int gain = dab + dcd - dac - dbd;
      if (gain > 0) {
        // dir 0: cut (a,b),(c,d) with b=succ(a), d=succ(c) -> reverse b..c
        // dir 1: cut (b,a),(d,c) with a=succ(b), c=succ(d) -> reverse a..d
        if (dir == 0)
          flipRange(POS[b], POS[c]);
        else
          flipRange(POS[a], POS[d]);
        CUR_LEN -= gain;
#ifdef TSP_DEBUG
        dbgValidate("2opt");
#endif
        pushCity(a);
        pushCity(b);
        pushCity(c);
        pushCity(d);
        return true;
      }
    }
  }
  return false;
}

// --------------------------------------------------------------- Or-opt -----
inline bool inSegment(int c, int ps, int L) {
  for (int k = 0; k < L; ++k) {
    int t = ps + k;
    if (t >= N) t -= N;
    if (T[t] == c) return true;
  }
  return false;
}

// Move the L cities starting at position ps so that they follow position pj.
// `rev` inserts them in reverse order.  Shifts the shorter side of the cycle.
void moveSegment(int ps, int L, int pj, bool rev) {
  int seg[8];
  for (int k = 0; k < L; ++k) {
    int t = ps + k;
    if (t >= N) t -= N;
    seg[k] = T[t];
  }
  if (rev)
    for (int k = 0; k < L / 2; ++k) std::swap(seg[k], seg[L - 1 - k]);

  const int D = ((pj - ps - L) % N + N) % N;
  const int Db = ((ps - pj - 1) % N + N) % N;

  if (D <= Db) {
    for (int t = 0; t <= D; ++t) {
      const int src = (ps + L + t) % N;
      const int dst = (ps + t) % N;
      const int c = T[src];
      T[dst] = c;
      POS[c] = dst;
    }
    const int base = (ps + D + 1) % N;
    for (int k = 0; k < L; ++k) {
      const int t = (base + k) % N;
      T[t] = seg[k];
      POS[seg[k]] = t;
    }
  } else {
    for (int t = 0; t < Db; ++t) {
      const int src = ((ps - 1 - t) % N + N) % N;
      int dst = src + L;
      if (dst >= N) dst -= N;
      const int c = T[src];
      T[dst] = c;
      POS[c] = dst;
    }
    const int base = (pj + 1) % N;
    for (int k = 0; k < L; ++k) {
      const int t = (base + k) % N;
      T[t] = seg[k];
      POS[seg[k]] = t;
    }
  }
}

bool tryOrOpt(int a) {
  for (int L = 1; L <= oroptMaxL; ++L) {
    if (L > N - 5) break;
    const int ps = POS[a];
    int lp = ps + L - 1;
    if (lp >= N) lp -= N;
    const int last = T[lp];
    const int p = prvCity(a);
    const int q = nxtCity(last);
    if (p == last || q == a) continue;
    const int remGain = distc(p, a) + distc(q, last) - distc(p, q);
    if (remGain <= 0) continue;

    for (int side = 0; side < 2; ++side) {
      const int base = side ? last : a;
      const int* nbr = &NI[(size_t)base * K];
      for (int k = 0; k < K; ++k) {
        const int c = nbr[k];
        for (int anch = 0; anch < oroptAnchors; ++anch) {
          const int u = anch ? prvCity(c) : c;
          const int v = anch ? c : nxtCity(c);
          if (inSegment(u, ps, L) || inSegment(v, ps, L)) continue;
          const int duv = distc(u, v);
          int net = remGain - (distc(u, a) + distc(last, v) - duv);
          if (net > 0) {
            moveSegment(ps, L, POS[u], false);
            CUR_LEN -= net;
#ifdef TSP_DEBUG
            dbgValidate("oropt-fwd");
#endif
            pushCity(p);
            pushCity(q);
            pushCity(a);
            pushCity(last);
            pushCity(u);
            pushCity(v);
            return true;
          }
          if (L > 1) {
            net = remGain - (distc(u, last) + distc(a, v) - duv);
            if (net > 0) {
              moveSegment(ps, L, POS[u], true);
              CUR_LEN -= net;
#ifdef TSP_DEBUG
              dbgValidate("oropt-rev");
#endif
              pushCity(p);
              pushCity(q);
              pushCity(a);
              pushCity(last);
              pushCity(u);
              pushCity(v);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

inline bool timeUp() { return elapsedSec() >= HardDeadline; }

// ----------------------------------------------------------------- 3-opt ----
// Cut the cycle at p1, p2, p3 (1 <= p1 < p2 < p3 <= N-1) so that four non-empty
// blocks exist:  A = [0,p1)  B = [p1,p2)  C = [p2,p3)  D = [p3,N).
// Only A's tail and D's head are touched at the junctions, and a 3-opt move can
// always be written as "put B and C in either order, each possibly reversed"
// with A and D left in place.  That keeps every move a rotation of [p1,p3) plus
// at most two contiguous reversals, i.e. no wrap-around bookkeeping at all.
// Reverse exactly the forward positions p1..p2, never the complement.
// `flipRange` may take the complement because for a stand-alone 2-opt move the
// resulting undirected cycle is identical, but inside a composite move the
// complement reversal would relocate the other block, so 3-opt needs this form.
void flipExact(int p1, int p2) {
  int len = (p2 - p1 + N) % N + 1;
  for (int k = 0; k < len / 2; ++k) {
    int i = p1 + k;
    if (i >= N) i -= N;
    int j = p2 - k;
    if (j < 0) j += N;
    const int ci = T[i], cj = T[j];
    T[i] = cj;
    T[j] = ci;
    POS[cj] = i;
    POS[ci] = j;
  }
}

void apply3opt(int p1, int p2, int p3, bool swapBC, bool flipB, bool flipC) {
  if (swapBC) {
    std::rotate(T.begin() + p1, T.begin() + p2, T.begin() + p3);
    for (int i = p1; i < p3; ++i) POS[T[i]] = i;
  }
  const int lenB = p2 - p1, lenC = p3 - p2;
  int bS, bE, cS, cE;
  if (swapBC) {
    cS = p1;
    cE = p1 + lenC;
    bS = cE;
    bE = p3;
  } else {
    bS = p1;
    bE = p2;
    cS = p2;
    cE = p3;
  }
  if (flipC && lenC >= 2) flipExact(cS, cE - 1);
  if (flipB && lenB >= 2) flipExact(bS, bE - 1);
}

// Three genuine 3-opt reconnections over the same cut set:
//   case 5  A C   B   D   gain = R - (aCs + cEbS + bEdS)   -- mostly free terms
//   case 4  A B^R C^R D   gain = R - (aBe + bScE + cSdS)
//   case 8  A C^R B^R D   gain = R - (aCe + cSbE + bSdS)
// with R = d(aB)+d(bC)+d(cD) the three removed edges.  d(aCs) and d(bScE) come
// straight from the candidate lists that drive the enumeration, so those two
// terms cost nothing.
bool try3opt(int x) {
  for (int role = 0; role < opt3optRoles; ++role) {
    const int p1 = role ? POS[x] : POS[x] + 1;
    if (p1 < 1 || p1 > N - 3) continue;
    const int aEnd = T[p1 - 1];
    const int bStart = T[p1];
    const int* naI = &NI[(size_t)aEnd * K];
    const int* naD = &ND[(size_t)aEnd * K];
    const int* nbI = &NI[(size_t)bStart * K];
    const int* nbD = &ND[(size_t)bStart * K];
    const int dAB = distc(aEnd, bStart);
    for (int i = 0; i < K; ++i) {
      const int cStart = naI[i];
      const int p2 = POS[cStart];
      if (p2 <= p1 || p2 > N - 2) continue;
      const int bEnd = T[p2 - 1];
      const int dBC = distc(bEnd, cStart);
      const int dACs = naD[i];   // d(aEnd, cStart): free
      const int dabEnd = distc(aEnd, bEnd);
      const int jmax = opt3optInner > 0 ? opt3optInner : K;
      for (int j = 0; j < jmax; ++j) {
        const int cEnd = nbI[j];
        const int p3 = POS[cEnd] + 1;
        if (p3 <= p2 || p3 > N - 1) continue;
        const int dStart = T[p3];
        const int R = dAB + dBC + distc(cEnd, dStart);
        const int dCSbE = nbD[j];  // d(bStart, cEnd): free

        // case 5: A C B D -> aEnd-cStart, cEnd-bStart, bEnd-dStart
        if (dACs + dCSbE < R) {
          const int dBEdS = distc(bEnd, dStart);
          if (dACs + dCSbE + dBEdS < R) {
            const int gain = R - dACs - dCSbE - dBEdS;
            apply3opt(p1, p2, p3, true, false, false);
            CUR_LEN -= gain;
#ifdef TSP_DEBUG
            dbgValidate("3opt-c5");
#endif
            pushCity(aEnd);
            pushCity(bStart);
            pushCity(bEnd);
            pushCity(cStart);
            pushCity(cEnd);
            pushCity(dStart);
            return true;
          }
        }
        // case 6: A C B^R D -> aEnd-cStart, cEnd-bEnd, bStart-dStart
        if (opt3optExtra) {
          const int dCEbE = distc(cEnd, bEnd);
          if (dACs + dCEbE < R) {
            const int dBSdS6 = distc(bStart, dStart);
            if (dACs + dCEbE + dBSdS6 < R) {
              const int gain = R - dACs - dCEbE - dBSdS6;
              apply3opt(p1, p2, p3, true, true, false);
              CUR_LEN -= gain;
#ifdef TSP_DEBUG
              dbgValidate("3opt-c6");
#endif
              pushCity(aEnd);
              pushCity(bStart);
              pushCity(bEnd);
              pushCity(cStart);
              pushCity(cEnd);
              pushCity(dStart);
              return true;
            }
          }
        }
        // case 7: A C^R B D -> aEnd-cEnd, cStart-bStart, bEnd-dStart
        if (opt3optExtra) {
          const int dCSbS = distc(cStart, bStart);
          const int dACe7 = distc(aEnd, cEnd);
          if (dACe7 + dCSbS < R) {
            const int dBEdS7 = distc(bEnd, dStart);
            if (dACe7 + dCSbS + dBEdS7 < R) {
              const int gain = R - dACe7 - dCSbS - dBEdS7;
              apply3opt(p1, p2, p3, true, false, true);
              CUR_LEN -= gain;
#ifdef TSP_DEBUG
              dbgValidate("3opt-c7");
#endif
              pushCity(aEnd);
              pushCity(bStart);
              pushCity(bEnd);
              pushCity(cStart);
              pushCity(cEnd);
              pushCity(dStart);
              return true;
            }
          }
        }
        // case 4: A B^R C^R D -> aEnd-bEnd, bStart-cEnd, cStart-dStart
        if (opt3optCase4 && dabEnd + dCSbE < R) {
          const int dCSdS = distc(cStart, dStart);
          if (dabEnd + dCSbE + dCSdS < R) {
            const int gain = R - dabEnd - dCSbE - dCSdS;
            apply3opt(p1, p2, p3, false, true, true);
            CUR_LEN -= gain;
#ifdef TSP_DEBUG
            dbgValidate("3opt-c4");
#endif
            pushCity(aEnd);
            pushCity(bStart);
            pushCity(bEnd);
            pushCity(cStart);
            pushCity(cEnd);
            pushCity(dStart);
            return true;
          }
        }
        // case 8: A C^R B^R D -> aEnd-cEnd, cStart-bEnd, bStart-dStart
        if (opt3optCase8) {
          const int dACe = distc(aEnd, cEnd);
          if (dACe < R) {
            const int dCSbE2 = distc(cStart, bEnd);
            if (dACe + dCSbE2 < R) {
              const int dBSdS = distc(bStart, dStart);
              if (dACe + dCSbE2 + dBSdS < R) {
                const int gain = R - dACe - dCSbE2 - dBSdS;
                apply3opt(p1, p2, p3, true, true, true);
                CUR_LEN -= gain;
#ifdef TSP_DEBUG
                dbgValidate("3opt-c8");
#endif
                pushCity(aEnd);
                pushCity(bStart);
                pushCity(bEnd);
                pushCity(cStart);
                pushCity(cEnd);
                pushCity(dStart);
                return true;
              }
            }
          }
        }
      }
    }
  }
  return false;
}

void optimizeCity(int c) {
  for (;;) {
    if (try2opt(c)) continue;
    if (tryOrOpt(c)) continue;
    if (opt3opt && try3opt(c)) continue;
    break;
  }
}

void runLocalSearch() {
  while (!QS.empty()) {
    if (timeUp()) return;
    const int c = QS.back();
    QS.pop_back();
    INQ[c] = 0;
    optimizeCity(c);
  }
}

// ----------------------------------------------------------- construction ---
void buildNearestNeighbor() {
  T.assign(N, 0);
  POS.assign(N, 0);
  std::vector<unsigned char> vis(N, 0);
  int cur = 0;
  vis[cur] = 1;
  T[0] = cur;
  for (int step = 1; step < N; ++step) {
    int bestC = -1;
    const int* nbr = &NI[(size_t)cur * K];
    for (int k = 0; k < K; ++k) {
      const int c = nbr[k];
      if (!vis[c]) {
        bestC = c;
        break;
      }
    }
    if (bestC < 0) {
      int bd = INT32_MAX;
      for (int j = 0; j < N; ++j) {
        if (vis[j]) continue;
        const int dd = distc(cur, j);
        if (dd < bd) {
          bd = dd;
          bestC = j;
        }
      }
    }
    cur = bestC;
    vis[cur] = 1;
    T[step] = cur;
  }
  rebuildPos();
}

// ----------------------------------------------------------- perturbation ---
// A B C D -> A C B D with four non-empty blocks.  Returns false when the
// instance is too small for a double bridge (the caller then keeps `seeds`
// untouched and falls back to a full local-search sweep).
bool doubleBridge(std::mt19937_64& rng, int seeds[6]) {
  if (N < 8) return false;
  int v[3];
  for (int i = 0; i < 3; ++i) {
    for (;;) {
      int cand;
      if (kickBias) {
        // Sample three tour edges and cut the longest of them: the local search
        // then has more to repair, so the kick disturbs a region that actually
        // matters instead of a random flat one.
        int bestLen = -1;
        cand = 1;
        for (int t = 0; t < kickBiasSamples; ++t) {
          const int p = 1 + (int)(rng() % (unsigned long long)(N - 1));
          const int len = distc(T[p - 1], T[p]);
          if (len > bestLen) {
            bestLen = len;
            cand = p;
          }
        }
      } else {
        cand = 1 + (int)(rng() % (unsigned long long)(N - 1));
      }
      bool dup = false;
      for (int j = 0; j < i; ++j)
        if (v[j] == cand) dup = true;
      if (!dup) {
        v[i] = cand;
        break;
      }
    }
  }
  std::sort(v, v + 3);
  const int a = v[0], b = v[1], c = v[2];

  // The three cut edges are (a-1,a), (b-1,b) and (c-1,c); the double bridge
  // reconnects them as A|C, C|B, B|D, so the length changes by `delta`.
  const int Alast = T[a - 1], Bfirst = T[a];
  const int Blast = T[b - 1], Cfirst = T[b];
  const int Clast = T[c - 1], Dfirst = T[c];
  seeds[0] = Alast;
  seeds[1] = Bfirst;
  seeds[2] = Blast;
  seeds[3] = Cfirst;
  seeds[4] = Clast;
  seeds[5] = Dfirst;
  const int delta = distc(Alast, Cfirst) + distc(Clast, Bfirst) + distc(Blast, Dfirst) -
                    distc(Alast, Bfirst) - distc(Blast, Cfirst) - distc(Clast, Dfirst);

  // A B C D -> A C B D: rotate the block B+C, i.e. exactly positions [a, c).
  std::rotate(T.begin() + a, T.begin() + b, T.begin() + c);
  for (int i = a; i < c; ++i) POS[T[i]] = i;
  CUR_LEN += delta;
#ifdef TSP_DEBUG
  dbgValidate("double-bridge");
#endif
  return true;
}

// ------------------------------------------------------------------- main ---
void writeTour(const char* path, const std::vector<int>& tour, const std::string& name) {
  FILE* f = std::fopen(path, "wb");
  if (!f) {
    std::fprintf(stderr, "cannot open tour output %s\n", path);
    std::exit(4);
  }
  std::fprintf(f, "NAME : %s_result\nTYPE : TOUR\nDIMENSION : %d\nTOUR_SECTION\n", name.c_str(), N);
  std::string line;
  line.reserve((size_t)N * 8 + 64);
  for (int i = 0; i < N; ++i) {
    char b[32];
    const int len = std::snprintf(b, sizeof(b), "%lld", OID[tour[i]]);
    line.append(b, (size_t)len);
    line.push_back('\n');
  }
  line.append("-1\nEOF\n");
  std::fwrite(line.data(), 1, line.size(), f);
  std::fclose(f);
}

std::string baseName(const std::string& p) {
  const size_t s = p.find_last_of("/\\");
  std::string b = (s == std::string::npos) ? p : p.substr(s + 1);
  const size_t d = b.find_last_of('.');
  if (d != std::string::npos) b = b.substr(0, d);
  return b;
}

}  // namespace

int main(int argc, char** argv) {
  T0 = Clock::now();

  std::string instance, output, metadata;
  double timeLimit = 60.0;
  long long seed = 1;
  int maxRestarts = 5;
  int kArg = 10;

  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (key == "--instance") instance = need("--instance");
    else if (key == "--output") output = need("--output");
    else if (key == "--time-limit") timeLimit = std::atof(need("--time-limit"));
    else if (key == "--seed") seed = std::atoll(need("--seed"));
    else if (key == "--max-restarts") maxRestarts = std::atoi(need("--max-restarts"));
    else if (key == "--metadata") metadata = need("--metadata");
    else if (key == "--k") kArg = std::atoi(need("--k"));
    else if (key == "--threeopt") opt3opt = std::atoi(need("--threeopt")) != 0;
    else if (key == "--threeopt-case8") opt3optCase8 = std::atoi(need("--threeopt-case8")) != 0;
    else if (key == "--threeopt-extra") opt3optExtra = std::atoi(need("--threeopt-extra")) != 0;
    else if (key == "--threeopt-roles") opt3optRoles = std::atoi(need("--threeopt-roles"));
    else if (key == "--threeopt-inner") opt3optInner = std::atoi(need("--threeopt-inner"));
    else if (key == "--accept-eq") acceptEq = std::atoi(need("--accept-eq")) != 0;
    else if (key == "--kicks") kickCount = std::atoi(need("--kicks"));
    else if (key == "--rrt-dev") rrtDev = std::atof(need("--rrt-dev"));
    else if (key == "--kick-bias") kickBias = std::atoi(need("--kick-bias"));
    else if (key == "--kick-bias-samples") kickBiasSamples = std::atoi(need("--kick-bias-samples"));
    else if (key == "--oropt-maxl") oroptMaxL = std::atoi(need("--oropt-maxl"));
    else if (key == "--threeopt-case4") opt3optCase4 = std::atoi(need("--threeopt-case4")) != 0;
    else if (key == "--oropt-anchors") oroptAnchors = std::atoi(need("--oropt-anchors"));
    else {
      std::fprintf(stderr, "unknown argument %s\n", key.c_str());
      return 2;
    }
  }
  if (instance.empty() || output.empty()) {
    std::fprintf(stderr,
                 "usage: --instance P --output P --time-limit S --seed N "
                 "--max-restarts N --metadata P [--k K]\n");
    return 2;
  }

  const double reserve = std::min(1.2, timeLimit * 0.03);
  HardDeadline = timeLimit - reserve;
  if (oroptMaxL < 1) oroptMaxL = 1;
  if (oroptMaxL > 8) oroptMaxL = 8;
  if (kickCount < 1) kickCount = 1;

  if (!parseInstance(instance.c_str())) {
    std::fprintf(stderr, "failed to parse instance %s\n", instance.c_str());
    return 3;
  }
  const double tParsed = elapsedSec();

  buildNeighbors(kArg);
  const double tNeighbors = elapsedSec();

  std::mt19937_64 rng((unsigned long long)seed * 0x9E3779B97F4A7C15ULL + 0xBF58476D1CE4E5B9ULL);

  buildNearestNeighbor();
  const long long lenConstruct = tourLength();
  CUR_LEN = lenConstruct;
  const double tConstruct = elapsedSec();

  QS.clear();
  QS.reserve(N);
  INQ.assign(N, 0);
  for (int i = N - 1; i >= 0; --i) pushCity(i);
  runLocalSearch();
  const long long lenFirstOpt = CUR_LEN;
  const long long recount = tourLength();
  if (recount != lenFirstOpt) {
    std::fprintf(stderr, "WARNING incremental length %lld != recount %lld\n", lenFirstOpt, recount);
    CUR_LEN = recount;
  }
  const double tFirstOpt = elapsedSec();

  std::vector<int> bestTour = T;
  long long bestLen = CUR_LEN;
  std::vector<int> curTour = T, curPos = POS;
  long long curLen = CUR_LEN;

  long long iterations = 0, accepted = 0;
  std::vector<char> trace;
  trace.resize(1 << 18);
  size_t traceLen = 0;
  traceLen += (size_t)std::snprintf(trace.data(), trace.size(),
                                    "{\"t\":%.3f,\"len\":%lld,\"kind\":\"first_local_opt\"}",
                                    tFirstOpt, lenFirstOpt);

  int seeds[6];
  while (!timeUp()) {
    ++iterations;
    bool ok = true;
    for (int k = 0; k < kickCount && ok; ++k) {
      if (doubleBridge(rng, seeds)) {
        for (int s = 0; s < 6; ++s) pushCity(seeds[s]);
      } else {
        ok = false;
      }
    }
    if (!ok) {
      for (int s = 0; s < N; ++s) pushCity(s);
    }
    runLocalSearch();

    // Acceptance.  Default: strict improvement of the incumbent local optimum.
    // `acceptEq` also takes equal-length plateaus.  `rrtDev > 0` switches to
    // record-to-record travel: accept anything no worse than the current record
    // times (1 + rrtDev), which lets the walk cross small barriers.
    bool better = (CUR_LEN < curLen) || (acceptEq && CUR_LEN == curLen);
    if (!better && rrtDev > 0.0) {
      better = (CUR_LEN <= bestLen + (long long)(bestLen * rrtDev));
    }
    if (better) {
      curLen = CUR_LEN;
      ++accepted;
      if (CUR_LEN < bestLen) {
        bestLen = CUR_LEN;
        bestTour = T;
        if (traceLen + 96 < trace.size()) {
          traceLen += (size_t)std::snprintf(trace.data() + traceLen, trace.size() - traceLen,
                                            ",{\"t\":%.3f,\"len\":%lld}", elapsedSec(), bestLen);
        }
      }
      curTour = T;
      curPos = POS;
    } else {
      T = curTour;
      POS = curPos;
      CUR_LEN = curLen;
    }
#ifdef TSP_DEBUG
    if ((iterations % 500) == 0) {
      const long long rc = tourLength();
      if (rc != CUR_LEN) {
        std::fprintf(stderr, "ACCOUNTING DRIFT at iter %lld: tracked %lld recount %lld\n",
                     iterations, CUR_LEN, rc);
        std::exit(7);
      }
      std::vector<unsigned char> seen(N, 0);
      for (int i = 0; i < N; ++i) {
        if (T[i] < 0 || T[i] >= N || seen[T[i]] || POS[T[i]] != i) {
          std::fprintf(stderr, "INVARIANT BROKEN at iter %lld\n", iterations);
          std::exit(8);
        }
        seen[T[i]] = 1;
      }
    }
#endif
  }
  const double tSearchEnd = elapsedSec();

  // Final exact recount of the incumbent.
  T = bestTour;
  rebuildPos();
  const long long finalLen = tourLength();
  if (finalLen != bestLen) {
    std::fprintf(stderr, "NOTE final recount %lld vs tracked %lld\n", finalLen, bestLen);
    bestLen = finalLen;
  }

  writeTour(output.c_str(), bestTour, baseName(instance));
  const double tEnd = elapsedSec();

  if (!metadata.empty()) {
    FILE* f = std::fopen(metadata.c_str(), "wb");
    if (f) {
      std::fprintf(f, "{\n");
      std::fprintf(f, "  \"searches_started\": 1,\n");
      std::fprintf(f, "  \"restarts_used\": 0,\n");
      std::fprintf(f, "  \"max_restarts_allowed\": %d,\n", maxRestarts);
      std::fprintf(f, "  \"algorithm\": \"v001 nn+2opt+oropt+double_bridge_ils\",\n");
      std::fprintf(f, "  \"seed\": %lld,\n", seed);
      std::fprintf(f, "  \"n\": %d,\n", N);
      std::fprintf(f, "  \"k_neighbors\": %d,\n", K);
      std::fprintf(f, "  \"three_opt\": %s,\n", opt3opt ? "true" : "false");
      std::fprintf(f, "  \"three_opt_case8\": %s,\n", opt3optCase8 ? "true" : "false");
      std::fprintf(f, "  \"three_opt_extra\": %s,\n", opt3optExtra ? "true" : "false");
      std::fprintf(f, "  \"three_opt_roles\": %d,\n", opt3optRoles);
      std::fprintf(f, "  \"three_opt_inner\": %d,\n", opt3optInner);
      std::fprintf(f, "  \"three_opt_case4\": %s,\n", opt3optCase4 ? "true" : "false");
      std::fprintf(f, "  \"oropt_anchors\": %d,\n", oroptAnchors);
      std::fprintf(f, "  \"accept_equal\": %s,\n", acceptEq ? "true" : "false");
      std::fprintf(f, "  \"kicks_per_iteration\": %d,\n", kickCount);
      std::fprintf(f, "  \"rrt_deviation\": %.6f,\n", rrtDev);
      std::fprintf(f, "  \"kick_bias\": %d,\n", kickBias);
      std::fprintf(f, "  \"kick_bias_samples\": %d,\n", kickBiasSamples);
      std::fprintf(f, "  \"oropt_max_segment\": %d,\n", oroptMaxL);
      std::fprintf(f, "  \"time_limit_seconds\": %.3f,\n", timeLimit);
      std::fprintf(f, "  \"construction_length\": %lld,\n", lenConstruct);
      std::fprintf(f, "  \"first_local_opt_length\": %lld,\n", lenFirstOpt);
      std::fprintf(f, "  \"best_length\": %lld,\n", bestLen);
      std::fprintf(f, "  \"ils_iterations\": %lld,\n", iterations);
      std::fprintf(f, "  \"ils_accepted\": %lld,\n", accepted);
      std::fprintf(f, "  \"t_parse\": %.4f,\n", tParsed);
      std::fprintf(f, "  \"t_neighbors\": %.4f,\n", tNeighbors);
      std::fprintf(f, "  \"t_construct\": %.4f,\n", tConstruct);
      std::fprintf(f, "  \"t_first_local_opt\": %.4f,\n", tFirstOpt);
      std::fprintf(f, "  \"t_search_end\": %.4f,\n", tSearchEnd);
      std::fprintf(f, "  \"t_total\": %.4f,\n", tEnd);
      std::fprintf(f, "  \"trace\": [%.*s]\n", (int)traceLen, trace.data());
      std::fprintf(f, "}\n");
      std::fclose(f);
    }
  }
  return 0;
}
