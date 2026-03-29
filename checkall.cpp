#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace std;

/*
  checkall.cpp

  Verifiziert Proposition 5 auf dem kritischen Kern
    Lambda_crit = {0,2,14,24}

  Für jede nichtleere Teilmenge H von {0,2,14,24} wird geprüft:
    - gibt es in Modus A eine zulässige Belegung?
    - falls nein: gibt es in Modus B eine zulässige Belegung?

  Eine Belegung ist zulässig, wenn:
    1. jeder Kontext lambda einen Zustand aus Sigma_M(lambda) bekommt
    2. für jede induzierte Psi-Kante u -> v im Teilgraphen H gilt:
         f(u) -> f(v) ist eine erlaubte REAL_EDGE

  WICHTIG:
    Dieses Programm prüft nur die tatsächlichen gerichteten Psi-Kanten.
    Es verlangt NICHT zusätzlich die Rückkante.
*/

// -----------------------------------------------------------------------------
// Symbolische Zustände
// -----------------------------------------------------------------------------

struct SymState {
    int s;  // z.B. 1 für S1
    int d;  // debt, 0 oder 2
};

static inline bool operator<(const SymState& a, const SymState& b) {
    if (a.s != b.s) return a.s < b.s;
    return a.d < b.d;
}

static inline bool operator==(const SymState& a, const SymState& b) {
    return a.s == b.s && a.d == b.d;
}

static inline string state_to_string(const SymState& x) {
    ostringstream oss;
    oss << "S" << x.s << "[" << x.d << "]";
    return oss.str();
}

// -----------------------------------------------------------------------------
// Kritischer Kern
// -----------------------------------------------------------------------------

static const vector<int> CORE = {0, 2, 14, 24};

static inline string ctx_to_string(int ctx) {
    ostringstream oss;
    oss << "lambda_" << ctx;
    return oss.str();
}

// -----------------------------------------------------------------------------
// Exakte REAL_EDGES aus real_edges_block.txt
// -----------------------------------------------------------------------------

static const int REAL_EDGES[][4] = {
    {3,0,6,0},
    {7,0,4,0},
    {4,0,5,0},
    {5,0,5,0},
    {4,0,2,0},
    {2,0,6,0},
    {6,0,6,0},
    {6,0,7,0},
    {7,0,1,0},
    {1,0,1,0},
    {0,2,7,0},
    {7,0,0,2},
    {1,0,4,0},
    {4,0,4,0},
    {2,0,4,0},
    {2,0,3,0},
    {3,0,5,0},
    {5,0,4,0},
    {3,0,3,0},
    {3,0,4,0},
    {4,0,3,0},
    {5,0,2,0},
    {1,0,0,2},
    {0,2,6,2},
    {6,2,7,0},
    {3,0,2,0},
    {2,0,2,0},
    {2,0,5,0},
    {5,0,3,0},
    {6,2,6,2},
    {6,0,3,0},
    {4,0,6,0},
    {6,0,5,0},
    {4,0,1,0},
    {4,0,0,2},
    {5,0,6,0},
    {6,0,2,0},
};

static bool state_edge_ok(const SymState& a, const SymState& b) {
    for (const auto& e : REAL_EDGES) {
        if (e[0] == a.s && e[1] == a.d && e[2] == b.s && e[3] == b.d) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Support-Mengen auf dem 4-core
// -----------------------------------------------------------------------------

enum class Mode { A, B };

static const map<int, vector<SymState>> SIGMA_A = {
    {0,  {{1,0}}},
    {2,  {{1,0}, {4,0}}},
    {14, {{1,0}, {4,0}, {7,0}}},
    {24, {{1,0}, {4,0}, {7,0}}},
};

static const map<int, vector<SymState>> SIGMA_B = {
    {0,  {{2,0}}},
    {2,  {{2,0}}},
    {14, {{2,0}, {3,0}, {4,0}, {5,0}, {6,0}}},
    {24, {{2,0}, {3,0}, {4,0}, {5,0}, {6,0}}},
};

static const map<int, string> CTX_DESC = {
    {0,  "(C,E,B,R1,pos=1,parity=1,debt=0)"},
    {2,  "(E,B,E,R1,pos=2,parity=0,debt=0)"},
    {14, "(D,C,E,R2,pos=6,parity=0,debt=0)"},
    {24, "(D,C,E,R3,pos=4,parity=0,debt=0)"},
};

// -----------------------------------------------------------------------------
// Exakte induzierte Psi-Kanten auf dem 4-core
// -----------------------------------------------------------------------------

static const vector<pair<int,int>> CORE_PSI = {
    {0,2},
    {14,0},
    {24,0},
};

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

static vector<int> subset_from_mask(int mask) {
    vector<int> H;
    for (int i = 0; i < (int)CORE.size(); ++i) {
        if (mask & (1 << i)) H.push_back(CORE[i]);
    }
    return H;
}

static string subset_to_string(const vector<int>& H) {
    ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < H.size(); ++i) {
        if (i) oss << ",";
        oss << H[i];
    }
    oss << "}";
    return oss.str();
}

static bool in_subset(const vector<int>& H, int x) {
    return find(H.begin(), H.end(), x) != H.end();
}

static vector<pair<int,int>> induced_edges(const vector<int>& H) {
    vector<pair<int,int>> E;
    for (const auto& [u,v] : CORE_PSI) {
        if (in_subset(H, u) && in_subset(H, v)) {
            E.push_back({u,v});
        }
    }
    return E;
}

static const map<int, vector<SymState>>& sigma_for_mode(Mode m) {
    return (m == Mode::A ? SIGMA_A : SIGMA_B);
}

static string mode_to_string(Mode m) {
    return (m == Mode::A ? "A" : "B");
}

// -----------------------------------------------------------------------------
// Solver
// -----------------------------------------------------------------------------

struct SearchResult {
    bool ok = false;
    map<int, SymState> assign;
};

static bool partial_assignment_ok(
    const map<int, SymState>& assign,
    const vector<pair<int,int>>& edges
) {
    for (const auto& [u,v] : edges) {
        auto it_u = assign.find(u);
        auto it_v = assign.find(v);
        if (it_u != assign.end() && it_v != assign.end()) {
            if (!state_edge_ok(it_u->second, it_v->second)) {
                return false;
            }
        }
    }
    return true;
}

static bool dfs_assign(
    const vector<int>& H,
    const vector<pair<int,int>>& edges,
    const map<int, vector<SymState>>& sigma,
    size_t idx,
    map<int, SymState>& assign
) {
    if (idx == H.size()) return true;

    int lambda = H[idx];
    auto it = sigma.find(lambda);
    if (it == sigma.end()) return false;

    for (const auto& s : it->second) {
        assign[lambda] = s;
        if (partial_assignment_ok(assign, edges) &&
            dfs_assign(H, edges, sigma, idx + 1, assign)) {
            return true;
        }
        assign.erase(lambda);
    }
    return false;
}

static SearchResult solve_subset(const vector<int>& H, Mode mode) {
    SearchResult res;
    const auto& sigma = sigma_for_mode(mode);
    auto edges = induced_edges(H);

    map<int, SymState> assign;
    if (dfs_assign(H, edges, sigma, 0, assign)) {
        res.ok = true;
        res.assign = std::move(assign);
    }
    return res;
}

// -----------------------------------------------------------------------------
// Pretty printing
// -----------------------------------------------------------------------------

static string assignment_to_string(const map<int, SymState>& assign) {
    ostringstream oss;
    bool first = true;
    for (const auto& [ctx, st] : assign) {
        if (!first) oss << ", ";
        first = false;
        oss << ctx_to_string(ctx) << "=" << state_to_string(st);
    }
    return oss.str();
}

static void print_header() {
    cout << "========================================\n";
    cout << "Critical-core checker for Proposition 5\n";
    cout << "========================================\n\n";

    cout << "Core contexts:\n";
    for (int ctx : CORE) {
        cout << "  " << setw(2) << ctx << " : " << CTX_DESC.at(ctx) << "\n";
    }
    cout << "\n";

    cout << "Mode A supports:\n";
    for (int ctx : CORE) {
        cout << "  " << setw(2) << ctx << " : ";
        bool first = true;
        for (const auto& s : SIGMA_A.at(ctx)) {
            if (!first) cout << " ";
            first = false;
            cout << state_to_string(s);
        }
        cout << "\n";
    }
    cout << "\n";

    cout << "Mode B supports:\n";
    for (int ctx : CORE) {
        cout << "  " << setw(2) << ctx << " : ";
        bool first = true;
        for (const auto& s : SIGMA_B.at(ctx)) {
            if (!first) cout << " ";
            first = false;
            cout << state_to_string(s);
        }
        cout << "\n";
    }
    cout << "\n";

    cout << "Induced CORE_PSI edges:\n";
    for (const auto& [u,v] : CORE_PSI) {
        cout << "  " << ctx_to_string(u) << " -> " << ctx_to_string(v) << "\n";
    }
    cout << "\n";
}

static void print_latex_row(
    const vector<int>& H,
    Mode mode,
    const map<int, SymState>& assign
) {
    cout << "$\\{";
    for (size_t i = 0; i < H.size(); ++i) {
        if (i) cout << ",";
        cout << "\\lambda_" << H[i];
    }
    cout << "\\}$ & $" << mode_to_string(mode) << "$ & $(";

    bool first = true;
    for (int ctx : H) {
        if (!first) cout << ", ";
        first = false;
        auto it = assign.find(ctx);
        if (it == assign.end()) {
            cout << "?";
        } else {
            cout << state_to_string(it->second);
        }
    }
    cout << ")$ \\\\\n";
}

// -----------------------------------------------------------------------------
// checkall()
// -----------------------------------------------------------------------------

static int checkall(bool print_latex = true) {
    print_header();

    cout << "Checking all 15 nonempty subsets...\n\n";

    if (print_latex) {
        cout << "LaTeX rows:\n";
        cout << "----------------------------------------\n";
    }

    bool all_ok = true;

    for (int mask = 1; mask < (1 << (int)CORE.size()); ++mask) {
        vector<int> H = subset_from_mask(mask);

        SearchResult a = solve_subset(H, Mode::A);
        SearchResult b;
        if (!a.ok) {
            b = solve_subset(H, Mode::B);
        }

        if (a.ok) {
            cout << "H = " << subset_to_string(H)
                 << " -> mode A -> "
                 << assignment_to_string(a.assign) << "\n";
            if (print_latex) print_latex_row(H, Mode::A, a.assign);
        } else if (b.ok) {
            cout << "H = " << subset_to_string(H)
                 << " -> mode B -> "
                 << assignment_to_string(b.assign) << "\n";
            if (print_latex) print_latex_row(H, Mode::B, b.assign);
        } else {
            all_ok = false;
            cout << "H = " << subset_to_string(H)
                 << " -> NO WITNESS FOUND (simultaneous obstruction)\n";
        }
    }

    cout << "\n";
    if (all_ok) {
        cout << "VERIFIED: no simultaneous obstruction on the critical core.\n";
        return 0;
    } else {
        cout << "FAILED: at least one subset has no witness in either mode.\n";
        return 1;
    }
}

// -----------------------------------------------------------------------------
// main()
// -----------------------------------------------------------------------------

int main() {
    return checkall(true);
}