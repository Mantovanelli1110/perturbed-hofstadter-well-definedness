#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 4096
#define MAX_PROFILES 512
#define MAX_EDGES 4096
#define MAX_CONTEXTS 20000
#define MAX_SUCC 16

/* ------------------------------------------------------------
   5-state alphabet
   ------------------------------------------------------------ */

typedef enum { A_ST=0, B_ST=1, C_ST=2, D_ST=3, E_ST=4, INVALID_ST=-1 } State5;

static const char *state_name(State5 s) {
    switch (s) {
        case A_ST: return "A";
        case B_ST: return "B";
        case C_ST: return "C";
        case D_ST: return "D";
        case E_ST: return "E";
        default: return "?";
    }
}

static State5 char_to_state(char c) {
    switch (c) {
        case 'A': return A_ST;
        case 'B': return B_ST;
        case 'C': return C_ST;
        case 'D': return D_ST;
        case 'E': return E_ST;
        default: return INVALID_ST;
    }
}

static int allowed_edge(State5 u, State5 v) {
    return
        (u == A_ST && v == D_ST) ||
        (u == B_ST && v == E_ST) ||
        (u == C_ST && v == E_ST) ||
        (u == D_ST && v == C_ST) ||
        (u == D_ST && v == D_ST) ||
        (u == E_ST && v == A_ST) ||
        (u == E_ST && v == B_ST);
}

/* ------------------------------------------------------------
   R1, R2, R3 codewords
   ------------------------------------------------------------ */

typedef enum { R1=0, R2=1, R3=2, INVALID_R=-1 } RCode;

static const char *rcode_name(RCode r) {
    switch (r) {
        case R1: return "R1";
        case R2: return "R2";
        case R3: return "R3";
        default: return "?";
    }
}

static const char *rcode_word(RCode r) {
    switch (r) {
        case R1: return "EB";
        case R2: return "EADDDC";
        case R3: return "EADC";
        default: return "";
    }
}

static int rcode_len(RCode r) {
    return (int)strlen(rcode_word(r));
}

static State5 rcode_letter(RCode r, int p1) {
    /* p1 is 1-based */
    const char *w = rcode_word(r);
    if (p1 < 1 || p1 > (int)strlen(w)) return INVALID_ST;
    return char_to_state(w[p1 - 1]);
}

/* ------------------------------------------------------------
   Compressed profile classes S_i
   ------------------------------------------------------------ */

typedef struct {
    int weight;
    int minpref;
    char first;
    char last;
} SigClass;

static SigClass SCLASS[9] = {
    {-2, -2, '-', '-'}, /* S0 */
    { 0, -2, '-', '+'}, /* S1 */
    { 0, -1, '+', '+'}, /* S2 */
    { 0, -1, '+', '-'}, /* S3 */
    { 0, -1, '-', '+'}, /* S4 */
    { 0, -1, '-', '-'}, /* S5 */
    { 0,  0, '+', '-'}, /* S6 */
    { 2,  0, '+', '+'}, /* S7 */
    { 2,  0, '+', '-'}  /* S8 */
};

typedef struct {
    int sid;   /* S_i */
    int debt;  /* 0 or 2 */
} CompState;

static const CompState REACHABLE[9] = {
    {0,2}, {1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {6,2}, {7,0}
};

static int is_reachable_compstate(int sid, int debt) {
    for (int i = 0; i < 9; i++) {
        if (REACHABLE[i].sid == sid && REACHABLE[i].debt == debt) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------
   Observed compressed edges from the stabilized automaton
   ------------------------------------------------------------ */

typedef struct {
    int from_sid, from_debt;
    int to_sid, to_debt;
} CompEdge;

static CompEdge OBS_EDGES[128];
static int OBS_EDGE_COUNT = 0;

static int observed_edge_exists(int fs, int fd, int ts, int td) {
    for (int i = 0; i < OBS_EDGE_COUNT; i++) {
        if (OBS_EDGES[i].from_sid == fs &&
            OBS_EDGES[i].from_debt == fd &&
            OBS_EDGES[i].to_sid == ts &&
            OBS_EDGES[i].to_debt == td) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------
   Profile CSV
   ------------------------------------------------------------ */

typedef struct {
    int profile_id;
    int len;
    char word[128];
    int weight;
    int minpref;
    char first;
    char last;
    int sid;
} RawProfile;

static RawProfile RAW_PROFILES[MAX_PROFILES];
static int RAW_PROFILE_COUNT = 0;

static int sigclass_of_profile(int weight, int minpref, char first, char last) {
    for (int i = 0; i < 9; i++) {
        if (SCLASS[i].weight == weight &&
            SCLASS[i].minpref == minpref &&
            SCLASS[i].first == first &&
            SCLASS[i].last == last) {
            return i;
        }
    }
    return -1;
}

static void load_profiles_csv(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", filename);
        exit(1);
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty profiles file\n");
        exit(1);
    }

    while (fgets(line, sizeof(line), fp)) {
        RawProfile rp;
        memset(&rp, 0, sizeof(rp));

        char first[8], last[8];
        if (sscanf(line, "%d,%d,%127[^,],%d,%d,%7[^,],%7s",
                   &rp.profile_id,
                   &rp.len,
                   rp.word,
                   &rp.weight,
                   &rp.minpref,
                   first,
                   last) != 7) {
            continue;
        }

        rp.first = first[0];
        rp.last = last[0];
        rp.sid = sigclass_of_profile(rp.weight, rp.minpref, rp.first, rp.last);

        if (RAW_PROFILE_COUNT >= MAX_PROFILES) {
            fprintf(stderr, "Too many profiles\n");
            exit(1);
        }
        RAW_PROFILES[RAW_PROFILE_COUNT++] = rp;
    }

    fclose(fp);
}

/* ------------------------------------------------------------
   Edge CSV on raw profiles -> compress to observed edges
   ------------------------------------------------------------ */

static int raw_profile_sid(int pid) {
    for (int i = 0; i < RAW_PROFILE_COUNT; i++) {
        if (RAW_PROFILES[i].profile_id == pid) return RAW_PROFILES[i].sid;
    }
    return -1;
}

static void add_observed_edge(int fs, int fd, int ts, int td) {
    if (!observed_edge_exists(fs, fd, ts, td)) {
        if (OBS_EDGE_COUNT >= 128) {
            fprintf(stderr, "Too many compressed edges\n");
            exit(1);
        }
        OBS_EDGES[OBS_EDGE_COUNT++] = (CompEdge){fs, fd, ts, td};
    }
}

static void load_edges_csv(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", filename);
        exit(1);
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty edges file\n");
        exit(1);
    }

    while (fgets(line, sizeof(line), fp)) {
        int fp_id, fd, tp_id, td;
        unsigned long long cnt;

        if (sscanf(line, "%d,%d,%d,%d,%llu",
                   &fp_id, &fd, &tp_id, &td, &cnt) != 5) {
            continue;
        }

        int fs = raw_profile_sid(fp_id);
        int ts = raw_profile_sid(tp_id);
        if (fs < 0 || ts < 0) continue;

        add_observed_edge(fs, fd, ts, td);
    }

    fclose(fp);
}

/* ------------------------------------------------------------
   Local contexts
   ------------------------------------------------------------ */

typedef struct {
    State5 xm1, x0, xp1;
    RCode rho;
    int pos;       /* 1-based position in rho */
    int parity;    /* 0/1 */
    int debt;      /* 0 or 2 */
} LocalCtx;

static LocalCtx CTX[MAX_CONTEXTS];
static int CTX_COUNT = 0;

static int ctx_equal(const LocalCtx *a, const LocalCtx *b) {
    return a->xm1 == b->xm1 &&
           a->x0 == b->x0 &&
           a->xp1 == b->xp1 &&
           a->rho == b->rho &&
           a->pos == b->pos &&
           a->parity == b->parity &&
           a->debt == b->debt;
}

static int ctx_index(const LocalCtx *c) {
    for (int i = 0; i < CTX_COUNT; i++) {
        if (ctx_equal(&CTX[i], c)) return i;
    }
    return -1;
}

static int add_ctx_if_new(const LocalCtx *c) {
    int idx = ctx_index(c);
    if (idx >= 0) return idx;
    if (CTX_COUNT >= MAX_CONTEXTS) {
        fprintf(stderr, "Too many contexts\n");
        exit(1);
    }
    CTX[CTX_COUNT] = *c;
    return CTX_COUNT++;
}

static int boundary_predecessor_ok(State5 xm1, RCode rho, int pos) {
    State5 x0 = rcode_letter(rho, pos);

    if (pos > 1) {
        return xm1 == rcode_letter(rho, pos - 1);
    }

    /* pos = 1, predecessor comes from previous codeword */
    for (int prev = R1; prev <= R3; prev++) {
        State5 last = rcode_letter((RCode)prev, rcode_len((RCode)prev));
        if (allowed_edge(last, x0) && xm1 == last) return 1;
    }
    return 0;
}

static int boundary_successor_ok(State5 xp1, RCode rho, int pos) {
    State5 x0 = rcode_letter(rho, pos);

    if (pos < rcode_len(rho)) {
        return xp1 == rcode_letter(rho, pos + 1);
    }

    /* pos = end, successor comes from next codeword */
    for (int nxt = R1; nxt <= R3; nxt++) {
        State5 first = rcode_letter((RCode)nxt, 1);
        if (allowed_edge(x0, first) && xp1 == first) return 1;
    }
    return 0;
}

static int admissible_ctx(const LocalCtx *c) {
    if (!(c->debt == 0 || c->debt == 2)) return 0;
    if (!(c->parity == 0 || c->parity == 1)) return 0;

    State5 expected = rcode_letter(c->rho, c->pos);
    if (expected == INVALID_ST || c->x0 != expected) return 0;

    if (!allowed_edge(c->xm1, c->x0)) return 0;
    if (!allowed_edge(c->x0, c->xp1)) return 0;

    if (!boundary_predecessor_ok(c->xm1, c->rho, c->pos)) return 0;
    if (!boundary_successor_ok(c->xp1, c->rho, c->pos)) return 0;

    return 1;
}

static void enumerate_admissible_contexts(void) {
    for (int rho = R1; rho <= R3; rho++) {
        int len = rcode_len((RCode)rho);
        for (int pos = 1; pos <= len; pos++) {
            State5 x0 = rcode_letter((RCode)rho, pos);
            for (int xm1 = A_ST; xm1 <= E_ST; xm1++) {
                for (int xp1 = A_ST; xp1 <= E_ST; xp1++) {
                    for (int parity = 0; parity <= 1; parity++) {
                        for (int di = 0; di < 2; di++) {
                            int debt = (di == 0 ? 0 : 2);
                            LocalCtx c;
                            c.xm1 = (State5)xm1;
                            c.x0 = x0;
                            c.xp1 = (State5)xp1;
                            c.rho = (RCode)rho;
                            c.pos = pos;
                            c.parity = parity;
                            c.debt = debt;
                            if (admissible_ctx(&c)) add_ctx_if_new(&c);
                        }
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------
   Successor relation Ψ
   ------------------------------------------------------------ */

typedef struct {
    int src_idx;
    int dst_idx;
} CtxEdge;

static CtxEdge PSI_EDGES[200000];
static int PSI_COUNT = 0;

static int psi_edge_exists(int s, int t) {
    for (int i = 0; i < PSI_COUNT; i++) {
        if (PSI_EDGES[i].src_idx == s && PSI_EDGES[i].dst_idx == t) return 1;
    }
    return 0;
}

static void add_psi_edge(int s, int t) {
    if (!psi_edge_exists(s, t)) {
        PSI_EDGES[PSI_COUNT++] = (CtxEdge){s, t};
    }
}

static void successors_of_ctx(const LocalCtx *c, LocalCtx out[MAX_SUCC], int *nout) {
    *nout = 0;

    if (c->pos < rcode_len(c->rho)) {
        /* advance inside same codeword */
        LocalCtx nc = *c;
        nc.pos++;
        nc.parity ^= 1;

        /* shift 3-window */
        nc.xm1 = c->x0;
        nc.x0  = rcode_letter(c->rho, nc.pos);
        if (nc.pos < rcode_len(c->rho)) {
            nc.xp1 = rcode_letter(c->rho, nc.pos + 1);
        } else {
            /* last position in codeword: choose compatible next-code first letter later */
            for (int nr = R1; nr <= R3; nr++) {
                State5 first = rcode_letter((RCode)nr, 1);
                if (allowed_edge(nc.x0, first)) {
                    LocalCtx tmp = nc;
                    tmp.xp1 = first;
                    if (admissible_ctx(&tmp)) out[(*nout)++] = tmp;
                }
            }
            return;
        }

        if (admissible_ctx(&nc)) out[(*nout)++] = nc;
        return;
    }

    /* move to first position of a successor codeword */
    for (int nr = R1; nr <= R3; nr++) {
        LocalCtx nc;
        nc.rho = (RCode)nr;
        nc.pos = 1;
        nc.parity = c->parity ^ 1;
        nc.debt = c->debt; /* micro-step keeps debt; macro-level debt is in π */

        nc.xm1 = c->x0;
        nc.x0 = rcode_letter((RCode)nr, 1);

        if (!allowed_edge(nc.xm1, nc.x0)) continue;

        if (rcode_len((RCode)nr) >= 2) {
            nc.xp1 = rcode_letter((RCode)nr, 2);
        } else {
            continue;
        }

        if (admissible_ctx(&nc)) out[(*nout)++] = nc;
    }
}

static void build_psi_relation(void) {
    for (int i = 0; i < CTX_COUNT; i++) {
        LocalCtx succs[MAX_SUCC];
        int ns = 0;
        successors_of_ctx(&CTX[i], succs, &ns);
        for (int k = 0; k < ns; k++) {
            int j = ctx_index(&succs[k]);
            if (j >= 0) add_psi_edge(i, j);
        }
    }
}

static void print_ctx_compact(int idx) {
    const LocalCtx *c = &CTX[idx];
    printf("ctx %d: (%s,%s,%s,%s,pos=%d,parity=%d,debt=%d)",
           idx,
           state_name(c->xm1),
           state_name(c->x0),
           state_name(c->xp1),
           rcode_name(c->rho),
           c->pos,
           c->parity,
           c->debt);
}

static int is_core_ctx_index(int idx) {
    return idx == 0 || idx == 2 || idx == 14 || idx == 24;
}

static void dump_core_psi_edges(void) {
    int core_count = 0;

    printf("\n");
    printf("========================================================\n");
    printf("Induced Psi-edge list on the 4-core {0,2,14,24}\n");
    printf("========================================================\n");

    printf("Core context descriptions:\n");
    for (int i = 0; i < CTX_COUNT; i++) {
        if (is_core_ctx_index(i)) {
            printf("  ");
            print_ctx_compact(i);
            printf("\n");
        }
    }

    printf("\n");
    printf("CORE_PSI edges (machine-readable):\n");
    for (int i = 0; i < PSI_COUNT; i++) {
        int s = PSI_EDGES[i].src_idx;
        int t = PSI_EDGES[i].dst_idx;
        if (is_core_ctx_index(s) && is_core_ctx_index(t)) {
            printf("CORE_PSI: %d -> %d\n", s, t);
            core_count++;
        }
    }

    printf("\n");
    printf("CORE_PSI edges (C initializer form):\n");
    printf("static const vector<pair<int,int>> CORE_PSI = {\n");
    for (int i = 0; i < PSI_COUNT; i++) {
        int s = PSI_EDGES[i].src_idx;
        int t = PSI_EDGES[i].dst_idx;
        if (is_core_ctx_index(s) && is_core_ctx_index(t)) {
            printf("    {%d,%d},\n", s, t);
        }
    }
    printf("};\n");

    printf("\n");
    printf("Core induced edge count = %d\n", core_count);
}

/* ------------------------------------------------------------
   Compression map π
   ------------------------------------------------------------ */

/* Honest note:
   This is a provisional symbolic compression.
   We map (rho,pos,debt) to one of the reachable compressed states
   by a simple explicit rule. This is the place to refine once the exact
   symbolic -> macro-profile correspondence is proved. */

static int pi_ctx_to_sid(const LocalCtx *c) {
    /* Coarse symbolic proxy:
       - contexts starting at E inside R2/R3 with debt 0 tend to positive classes,
       - D-heavy middle of R2 maps to negative classes,
       - debt 2 only maps to S0 or S6 classes.
       This is a placeholder but explicit and deterministic.
    */

    if (c->debt == 2) {
        if (c->rho == R2 && (c->pos >= 3 && c->pos <= 5)) return 0; /* S0[2] */
        return 6; /* S6[2] */
    }

    if (c->rho == R1) return 7; /* S7[0] */
    if (c->rho == R3) {
        if (c->pos == 1) return 1;
        if (c->pos == 2) return 4;
        if (c->pos == 3) return 3;
        return 6;
    }
    /* R2 */
    if (c->pos == 1) return 1;
    if (c->pos == 2) return 4;
    if (c->pos == 3) return 5;
    if (c->pos == 4) return 5;
    if (c->pos == 5) return 3;
    return 6;
}

static void pi_ctx_to_comp(const LocalCtx *c, int *sid, int *debt) {
    *sid = pi_ctx_to_sid(c);
    *debt = c->debt;
}

/* ------------------------------------------------------------
   Main verification
   ------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *profiles_csv = "D_macro_profiles_34.csv";
    const char *edges_csv = "D_debt_edges_34.csv";

    if (argc >= 2) profiles_csv = argv[1];
    if (argc >= 3) edges_csv = argv[2];

    load_profiles_csv(profiles_csv);
    load_edges_csv(edges_csv);

    enumerate_admissible_contexts();
    build_psi_relation();
    dump_core_psi_edges();

    int verified = 0;
    int failed = 0;

    printf("Loaded raw profiles: %d\n", RAW_PROFILE_COUNT);
    printf("Loaded compressed observed edges: %d\n", OBS_EDGE_COUNT);
    printf("Enumerated admissible contexts |Lambda_adm| = %d\n", CTX_COUNT);
    printf("Built local successor edges |Psi| = %d\n", PSI_COUNT);
    printf("Checking compressed closure...\n\n");

    for (int i = 0; i < PSI_COUNT; i++) {
        int sidx = PSI_EDGES[i].src_idx;
        int tidx = PSI_EDGES[i].dst_idx;

        int fs, fd, ts, td;
        pi_ctx_to_comp(&CTX[sidx], &fs, &fd);
        pi_ctx_to_comp(&CTX[tidx], &ts, &td);

        /* only meaningful if both ends land in reachable observed states */
        if (!is_reachable_compstate(fs, fd) || !is_reachable_compstate(ts, td)) {
            printf("UNMATCHED reachable compression:\n");
            printf("  src -> S%d[%d], dst -> S%d[%d]\n", fs, fd, ts, td);
            failed++;
            continue;
        }

        if (!observed_edge_exists(fs, fd, ts, td)) {
            printf("MISSING observed compressed edge:\n");
            printf("  S%d[%d] -> S%d[%d]\n", fs, fd, ts, td);
            failed++;
        } else {
            verified++;
        }
    }

    printf("\nSummary:\n");
    printf("  Verified local-successor compressions: %d\n", verified);
    printf("  Failed checks: %d\n", failed);

    if (failed == 0) {
        printf("  RESULT: all checked local symbolic successors compress to observed edges.\n");
    } else {
        printf("  RESULT: some local symbolic successors are not yet accounted for.\n");
    }

    return 0;
}
