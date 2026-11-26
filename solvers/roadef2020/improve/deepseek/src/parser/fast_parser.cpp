#include "fast_parser.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "json_objnames.hpp"
#include "../solution/solution.hpp"
#include "../util.hpp"

using namespace std;

namespace {

struct Cursor {
    const char *p;
    const char *end;
    const char *base;

    [[noreturn]] void fail(const char *msg) const {
        throw runtime_error(string("FastParser: ") + msg + " at byte " + to_string(p - base));
    }

    inline void ws() {
        while (p < end) {
            const char c = *p;
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') ++p;
            else break;
        }
    }

    inline void expect(char c) {
        ws();
        if (p >= end || *p != c) fail("unexpected character");
        ++p;
    }

    inline bool peek(char c) {
        ws();
        return p < end && *p == c;
    }

    /* skip a JSON string literal */
    inline void skip_string() {
        if (*p != '"') fail("string expected");
        ++p;
        while (p < end) {
            const char c = *p;
            if (c == '\\') { p += 2; continue; }
            if (c == '"') { ++p; return; }
            ++p;
        }
        fail("unterminated string");
    }

    /* read a JSON string literal into out (handles the escape-free fast path) */
    inline void read_string(string &out) {
        ws();
        if (p >= end || *p != '"') fail("string expected");
        ++p;
        const char *s = p;
        bool escaped = false;
        while (p < end) {
            const char c = *p;
            if (c == '\\') { escaped = true; p += 2; continue; }
            if (c == '"') break;
            ++p;
        }
        if (p >= end) fail("unterminated string");
        if (!escaped) {
            out.assign(s, p - s);
        } else {
            out.clear();
            for (const char *q = s; q < p; ++q) {
                if (*q == '\\' && q + 1 < p) {
                    ++q;
                    switch (*q) {
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        case 'r': out.push_back('\r'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'u': /* not present in this format */ out.push_back('?'); q += 4; break;
                        default: out.push_back(*q); break;
                    }
                } else {
                    out.push_back(*q);
                }
            }
        }
        ++p;  // closing quote
    }

    /* parse a JSON number as double; fast integer path first */
    inline double number() {
        ws();
        const char *s = p;
        bool neg = false;
        if (p < end && *p == '-') { neg = true; ++p; }
        const char *d0 = p;
        unsigned long long iv = 0;
        while (p < end) {
            const char c = *p;
            if (c >= '0' && c <= '9') {
                if (iv < 100000000000000000ULL) iv = iv * 10ULL + (unsigned long long)(c - '0');
                else { p = s; return slow_number(); }
                ++p;
            } else {
                break;
            }
        }
        if (p == d0) fail("number expected");
        if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) {
            p = s;
            return slow_number();
        }
        const double v = (double)iv;
        return neg ? -v : v;
    }

    inline double slow_number() {
        const char *s = p;
        while (p < end) {
            const char c = *p;
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++p;
            else break;
        }
        const size_t n = (size_t)(p - s);
        if (n == 0 || n >= 64) fail("bad number");
        char buf[64];
        memcpy(buf, s, n);
        buf[n] = '\0';
        return strtod(buf, nullptr);
    }

    inline uint_t unumber() {
        ws();
        if (p >= end) fail("integer expected");
        uint_t v = 0;
        bool any = false;
        while (p < end) {
            const char c = *p;
            if (c >= '0' && c <= '9') { v = v * 10u + (uint_t)(c - '0'); ++p; any = true; }
            else break;
        }
        if (!any) fail("integer expected");
        return v;
    }

    /* integers appear either bare (T, Delta, Scenarios_number) or quoted
     * ("tmax":"38", season members ["10","11",...]) in the official generator
     * output, so accept both representations. */
    inline uint_t uint_flexible() {
        ws();
        if (p < end && *p == '"') {
            ++p;
            uint_t v = 0;
            bool any = false;
            while (p < end) {
                const char c = *p;
                if (c >= '0' && c <= '9') { v = v * 10u + (uint_t)(c - '0'); ++p; any = true; }
                else break;
            }
            if (p >= end || *p != '"') fail("quoted integer expected");
            ++p;
            if (!any) fail("quoted integer expected");
            return v;
        }
        return unumber();
    }

    /* read a quoted integer key such as "17" */
    inline uint_t uint_key() {
        ws();
        if (p >= end || *p != '"') fail("quoted key expected");
        ++p;
        uint_t v = 0;
        while (p < end && *p >= '0' && *p <= '9') { v = v * 10u + (uint_t)(*p - '0'); ++p; }
        if (p >= end || *p != '"') fail("quoted integer key expected");
        ++p;
        return v;
    }

    void skip_value() {
        ws();
        if (p >= end) fail("value expected");
        const char c = *p;
        if (c == '"') { skip_string(); return; }
        if (c == '{') {
            ++p;
            ws();
            if (p < end && *p == '}') { ++p; return; }
            while (true) {
                ws();
                skip_string();
                ws();
                expect(':');
                skip_value();
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                expect('}');
                break;
            }
            return;
        }
        if (c == '[') {
            ++p;
            ws();
            if (p < end && *p == ']') { ++p; return; }
            while (true) {
                skip_value();
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                expect(']');
                break;
            }
            return;
        }
        if (c == 't') { p += 4; return; }
        if (c == 'f') { p += 5; return; }
        if (c == 'n') { p += 4; return; }
        (void)number();
    }

    /* count the members of an object whose '{' is at the cursor */
    uint_t count_members() {
        ws();
        expect('{');
        uint_t n = 0;
        ws();
        if (p < end && *p == '}') { ++p; return 0; }
        while (true) {
            ws();
            skip_string();
            ws();
            expect(':');
            skip_value();
            ++n;
            ws();
            if (p < end && *p == ',') { ++p; continue; }
            expect('}');
            break;
        }
        return n;
    }
};

struct Span {
    const char *b = nullptr;
    const char *e = nullptr;
    bool ok() const { return b != nullptr; }
};

}  // namespace

void FastParser::load_and_process(Instance *instance) {
    const int fd = open(path_to_file.c_str(), O_RDONLY);
    if (fd < 0) util::throw_err("Error while opening file: " + path_to_file);
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); util::throw_err("Error while stating file: " + path_to_file); }
    const size_t size = (size_t)st.st_size;
    const char *data = (const char *)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) util::throw_err("Error while mapping file: " + path_to_file);

    try {
        /* ---------- pass 1: locate top-level sections, count cardinalities -------- */
        Cursor c{data, data + size, data};
        c.ws();
        c.expect('{');
        map<string, Span> spans;
        uint_t n_resources = 0, n_interventions = 0;
        string key;
        c.ws();
        if (!(c.p < c.end && *c.p == '}')) {
            while (true) {
                c.ws();
                c.read_string(key);
                c.ws();
                c.expect(':');
                c.ws();
                if (key == RESOURCES || key == INTERVENTIONS || key == EXCLUSIONS) {
                    spans[key].b = c.p;
                    Cursor probe = c;
                    const uint_t n = probe.count_members();
                    if (key == RESOURCES) n_resources = n;
                    else if (key == INTERVENTIONS) n_interventions = n;
                    c.p = probe.p;
                    spans[key].e = c.p;
                } else if (key == SEASONS) {
                    spans[key].b = c.p;
                    c.skip_value();
                    spans[key].e = c.p;
                } else {
                    spans[key].b = c.p;
                    c.skip_value();
                    spans[key].e = c.p;
                }
                c.ws();
                if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
                c.expect('}');
                break;
            }
        }

        if (!spans.count(HORIZONS)) util::throw_err("FastParser: missing T");
        if (!spans.count(SCENARIOS)) util::throw_err("FastParser: missing Scenarios_number");

        Cursor t_c{spans[HORIZONS].b, spans[HORIZONS].e, data};
        const uint_t T = t_c.uint_flexible();
        Cursor q_c{spans[QUANTILE].b, spans[QUANTILE].e, data};
        const double quantile = q_c.number();
        Cursor a_c{spans[ALPHA].b, spans[ALPHA].e, data};
        const double alpha = a_c.number();
        uint_t comp_time = 15;
        if (spans.count(COMP_TIME)) {
            Cursor ct_c{spans[COMP_TIME].b, spans[COMP_TIME].e, data};
            comp_time = ct_c.uint_flexible();
        }

        instance->fill_horizons(T);
        instance->set_eval_parameters(quantile, alpha, comp_time);
        {
            Cursor s_c{spans[SCENARIOS].b, spans[SCENARIOS].e, data};
            s_c.expect('[');
            s_c.ws();
            if (!(s_c.p < s_c.end && *s_c.p == ']')) {
                while (true) {
                    instance->add_scenario(s_c.uint_flexible());
                    s_c.ws();
                    if (s_c.p < s_c.end && *s_c.p == ',') { ++s_c.p; continue; }
                    s_c.expect(']');
                    break;
                }
            }
        }

        instance->prepare_products(n_interventions, n_resources, T);
        instance->allocate_arrays();

        /* ---------- pass 2: fill sections (order matters: names before exclusions) */
        
        /* --- seasons table (row 0 unused; rows 1..k in name order like std::map) --- */
        map<string, uint_t> seas_names;
        instance->append_season();
        uint_t season_count = 1;
        if (spans.count(SEASONS)) {
            Cursor sc{spans[SEASONS].b, spans[SEASONS].e, data};
            /* gather names first (unsorted), then insert in sorted order */
            vector<string> names;
            {
                Cursor probe = sc;
                probe.expect('{');
                probe.ws();
                if (!(probe.p < probe.end && *probe.p == '}')) {
                    while (true) {
                        probe.ws();
                        string nm;
                        probe.read_string(nm);
                        names.push_back(nm);
                        probe.ws();
                        probe.expect(':');
                        probe.skip_value();
                        probe.ws();
                        if (probe.p < probe.end && *probe.p == ',') { ++probe.p; continue; }
                        probe.expect('}');
                        break;
                    }
                }
            }
            sort(names.begin(), names.end());
            for (const string &nm : names) {
                seas_names[nm] = season_count;
                instance->append_season();
                ++season_count;
            }
            sc.expect('{');
            sc.ws();
            if (!(sc.p < sc.end && *sc.p == '}')) {
                while (true) {
                    sc.ws();
                    string nm;
                    sc.read_string(nm);
                    sc.ws();
                    sc.expect(':');
                    sc.ws();
                    sc.expect('[');
                    sc.ws();
                    const uint_t row = seas_names[nm];
                    if (!(sc.p < sc.end && *sc.p == ']')) {
                        while (true) {
                            instance->append_to_season(row, sc.uint_flexible());
                            sc.ws();
                            if (sc.p < sc.end && *sc.p == ',') { ++sc.p; continue; }
                            sc.expect(']');
                            break;
                        }
                    } else {
                        sc.expect(']');
                    }
                    sc.ws();
                    if (sc.p < sc.end && *sc.p == ',') { ++sc.p; continue; }
                    sc.expect('}');
                    break;
                }
            }
        }

        /* --- resources --- */
        {
            const Span &sp = spans[RESOURCES];
            Cursor rc{sp.b, sp.e, data};
            rc.expect('{');
            rc.ws();
            if (!(rc.p < rc.end && *rc.p == '}')) {
                while (true) {
                    rc.ws();
                    string name;
                    rc.read_string(name);
                    const uint_t rid = instance->add_resource(name);
                    rc.ws();
                    rc.expect(':');
                    rc.ws();
                    rc.expect('{');
                    rc.ws();
                    if (!(rc.p < rc.end && *rc.p == '}')) {
                        while (true) {
                            rc.ws();
                            string what;
                            rc.read_string(what);
                            rc.ws();
                            rc.expect(':');
                            rc.ws();
                            rc.expect('[');
                            rc.ws();
                            uint_t idx = instance->get_ul_index(rid, 1);
                            if (!(rc.p < rc.end && *rc.p == ']')) {
                                while (true) {
                                    const double v = rc.number();
                                    if (what == MAX) instance->u[idx] = v;
                                    else if (what == MIN) instance->l[idx] = v;
                                    ++idx;
                                    rc.ws();
                                    if (rc.p < rc.end && *rc.p == ',') { ++rc.p; continue; }
                                    rc.expect(']');
                                    break;
                                }
                            } else {
                                rc.expect(']');
                            }
                            rc.ws();
                            if (rc.p < rc.end && *rc.p == ',') { ++rc.p; continue; }
                            rc.expect('}');
                            break;
                        }
                    } else {
                        rc.expect('}');
                    }
                    rc.ws();
                    if (rc.p < rc.end && *rc.p == ',') { ++rc.p; continue; }
                    rc.expect('}');
                    break;
                }
            }
        }

        /* --- interventions --- */
        {
            const Span &sp = spans[INTERVENTIONS];
            Cursor ic{sp.b, sp.e, data};
            ic.expect('{');
            ic.ws();
            if (!(ic.p < ic.end && *ic.p == '}')) {
                while (true) {
                    ic.ws();
                    string name;
                    ic.read_string(name);
                    const uint_t id = instance->add_intervention(name);
                    ic.ws();
                    ic.expect(':');
                    ic.ws();
                    ic.expect('{');
                    ic.ws();
                    if (!(ic.p < ic.end && *ic.p == '}')) {
                        while (true) {
                            ic.ws();
                            string what;
                            ic.read_string(what);
                            ic.ws();
                            ic.expect(':');
                            if (what == T_MAX) {
                                instance->add_t_max(id, ic.uint_flexible());
                            } else if (what == DELTA) {
                                ic.ws();
                                ic.expect('[');
                                ic.ws();
                                uint_t idx = instance->get_delta_index(id, 1);
                                if (!(ic.p < ic.end && *ic.p == ']')) {
                                    while (true) {
                                        instance->delta[idx++] = ic.uint_flexible();
                                        ic.ws();
                                        if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                        ic.expect(']');
                                        break;
                                    }
                                } else {
                                    ic.expect(']');
                                }
                            } else if (what == WORKLOAD) {
                                ic.ws();
                                ic.expect('{');
                                ic.ws();
                                if (!(ic.p < ic.end && *ic.p == '}')) {
                                    while (true) {
                                        ic.ws();
                                        string rname;
                                        ic.read_string(rname);
                                        const uint_t rid = instance->get_resource_id(rname);
                                        ic.ws();
                                        ic.expect(':');
                                        ic.ws();
                                        ic.expect('{');
                                        ic.ws();
                                        if (!(ic.p < ic.end && *ic.p == '}')) {
                                            while (true) {
                                                ic.ws();
                                                const uint_t t_int = ic.uint_key();
                                                ic.ws();
                                                ic.expect(':');
                                                ic.ws();
                                                ic.expect('{');
                                                ic.ws();
                                                if (!(ic.p < ic.end && *ic.p == '}')) {
                                                    while (true) {
                                                        ic.ws();
                                                        const uint_t st = ic.uint_key();
                                                        ic.ws();
                                                        ic.expect(':');
                                                        const double w = ic.number();
                                                        const uint_t idx = instance->get_r_index(id, st, rid, t_int);
                                                        instance->add_r(id, rid, idx, (half_fitness_t)w);
                                                        ic.ws();
                                                        if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                                        ic.expect('}');
                                                        break;
                                                    }
                                                } else {
                                                    ic.expect('}');
                                                }
                                                ic.ws();
                                                if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                                ic.expect('}');
                                                break;
                                            }
                                        } else {
                                            ic.expect('}');
                                        }
                                        ic.ws();
                                        if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                        ic.expect('}');
                                        break;
                                    }
                                } else {
                                    ic.expect('}');
                                }
                            } else if (what == RISK) {
                                ic.ws();
                                ic.expect('{');
                                ic.ws();
                                if (!(ic.p < ic.end && *ic.p == '}')) {
                                    while (true) {
                                        ic.ws();
                                        const uint_t t_int = ic.uint_key();
                                        ic.ws();
                                        ic.expect(':');
                                        ic.ws();
                                        ic.expect('{');
                                        ic.ws();
                                        if (!(ic.p < ic.end && *ic.p == '}')) {
                                            while (true) {
                                                ic.ws();
                                                const uint_t st = ic.uint_key();
                                                ic.ws();
                                                ic.expect(':');
                                                ic.ws();
                                                ic.expect('[');
                                                const uint_t ridx = instance->get_risk_index(id, st, t_int);
                                                half_fitness_vector_t &vec = instance->risk[ridx];
                                                ic.ws();
                                                if (!(ic.p < ic.end && *ic.p == ']')) {
                                                    while (true) {
                                                        vec.push_back((half_fitness_t)ic.number());
                                                        ic.ws();
                                                        if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                                        ic.expect(']');
                                                        break;
                                                    }
                                                } else {
                                                    ic.expect(']');
                                                }
                                                ic.ws();
                                                if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                                ic.expect('}');
                                                break;
                                            }
                                        } else {
                                            ic.expect('}');
                                        }
                                        ic.ws();
                                        if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                                        ic.expect('}');
                                        break;
                                    }
                                } else {
                                    ic.expect('}');
                                }
                            } else {
                                ic.skip_value();
                            }
                            ic.ws();
                            if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                            ic.expect('}');
                            break;
                        }
                    } else {
                        ic.expect('}');
                    }
                    ic.ws();
                    if (ic.p < ic.end && *ic.p == ',') { ++ic.p; continue; }
                    ic.expect('}');
                    break;
                }
            }
        }

        /* --- exclusions: same folding logic as Parser::process_exclusions --- */
        if (spans.count(EXCLUSIONS)) {
            vector<tuple<uint_t, uint_t, vector<string>>> filtered;
            unordered_map<unsigned long long, size_t> seen;
            {
                Cursor ec{spans[EXCLUSIONS].b, spans[EXCLUSIONS].e, data};
                ec.expect('{');
                ec.ws();
                if (!(ec.p < ec.end && *ec.p == '}')) {
                    while (true) {
                        ec.ws();
                        ec.skip_string();  // exclusion id, unused
                        ec.ws();
                        ec.expect(':');
                        ec.ws();
                        ec.expect('[');
                        ec.ws();
                        string n1, n2, season;
                        ec.read_string(n1);
                        ec.ws();
                        ec.expect(',');
                        ec.ws();
                        ec.read_string(n2);
                        ec.ws();
                        ec.expect(',');
                        ec.ws();
                        ec.read_string(season);
                        ec.ws();
                        /* tolerate optional extra elements */
                        while (ec.p < ec.end && *ec.p == ',') {
                            ++ec.p;
                            ec.ws();
                            string extra;
                            ec.read_string(extra);
                            ec.ws();
                        }
                        ec.expect(']');
                        uint_t id1 = instance->get_intervention_id(n1);
                        uint_t id2 = instance->get_intervention_id(n2);
                        if (id1 > id2) swap(id1, id2);
                        const unsigned long long k = ((unsigned long long)id1 << 32) | id2;
                        auto it = seen.find(k);
                        if (it == seen.end()) {
                            seen[k] = filtered.size();
                            filtered.push_back(make_tuple(id1, id2, vector<string>{season}));
                        } else {
                            get<2>(filtered[it->second]).push_back(season);
                        }
                        ec.ws();
                        if (ec.p < ec.end && *ec.p == ',') { ++ec.p; continue; }
                        ec.expect('}');
                        break;
                    }
                }
            }
            for (auto &trinity : filtered) {
                const string comb = util::combine_strings(get<2>(trinity));
                auto el = seas_names.find(comb);
                if (el != seas_names.end()) {
                    instance->add_exclusion(get<0>(trinity), get<1>(trinity), el->second);
                } else {
                    instance->append_season();
                    for (const string &s : get<2>(trinity)) {
                        auto si = seas_names.find(s);
                        if (si == seas_names.end()) util::throw_err("FastParser: unknown season '" + s + "'");
                        instance->combine_seasons(season_count, si->second);
                    }
                    instance->sort_season(season_count);
                    instance->add_exclusion(get<0>(trinity), get<1>(trinity), season_count);
                    seas_names[comb] = season_count++;
                }
            }
        }
    } catch (...) {
        munmap((void *)data, size);
        throw;
    }
    munmap((void *)data, size);
}

/* identical to Parser::process_avg_properties */
void FastParser::process_avg_properties(Instance *instance) {
    vector<pair<uint_t, fitness_t>> avg_deltas;
    vector<pair<uint_t, fitness_t>> avg_costs;
    vector<pair<uint_t, fitness_t>> avg_rds;

    std::default_random_engine engine(1);
    Solution s(instance, &engine);

    for (auto i : instance->get_interventions()) {
        auto delta_idx = instance->get_delta_index(i, 1);
        auto t_max = instance->get_t_max(i);
        fitness_t delta_avg = 0;
        fitness_t cost_avg = 0;
        fitness_t rd_avg = 0;
        for (uint_t t = 1; t <= t_max; ++t) {
            auto delta = instance->delta[delta_idx++];
            auto obj = s.estimate_schedule(i, t);
            cost_avg += obj.final_objective;
            rd_avg += obj.total_resource_use;
            delta_avg += delta;
        }
        if (t_max == 0) continue;
        delta_avg /= t_max;
        cost_avg /= t_max;
        rd_avg /= t_max;
        avg_deltas.emplace_back(i, delta_avg);
        avg_costs.emplace_back(i, cost_avg);
        avg_rds.emplace_back(i, rd_avg);
    }
    auto cmp_pair = [](const pair<uint_t, fitness_t> &a, const pair<uint_t, fitness_t> &b) {
        return a.second < b.second;
    };
    sort(avg_deltas.begin(), avg_deltas.end(), cmp_pair);
    sort(avg_costs.begin(), avg_costs.end(), cmp_pair);
    sort(avg_rds.begin(), avg_rds.end(), cmp_pair);
    instance->set_avg_deltas(avg_deltas);
    instance->set_avg_costs(avg_costs);
    instance->set_avg_rds(avg_rds);
}
