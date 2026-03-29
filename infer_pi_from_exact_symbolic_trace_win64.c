#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 8192
#define MAX_PATH 1000000
#define MAX_ECODE 100000
#define MAX_PROFILES 512
#define MAX_OBS_EDGES 256
#define MAX_COMP_STATES 16
#define MAX_CONTEXTS 4096
#define MAX_CTX_EDGES 32768
#define MAX_ANCHORS 200000

typedef enum { A_ST=0, B_ST=1, C_ST=2, D_ST=3, E_ST=4, INVALID_ST=-1 } State5;
typedef enum { R1=0, R2=1, R3=2, INVALID_R=-1 } RCode;

typedef struct {
    int path_pos;      /* 0-based index in PATH_STATES */
    RCode rho;         /* current return-word code */
    int pos_in_rho;    /* 1-based position inside rho */
    int ecode_index;   /* which R_i occurrence */
} ParsePos;

typedef struct {
    int weight;
    int minpref;
    char first;
    char last;
} SigClass;

typedef struct {
    int sid;
    int debt;
} CompState;

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

typedef struct {
    int from_idx;
    int to_idx;
} ObsEdge;

typedef struct {
    State5 xm1, x0, xp1;
    RCode rho;
    int pos;       /* 1-based */
    int parity;    /* 0/1 */
    int debt;      /* 0 or 2 */
} LocalCtx;

typedef struct {
    int src_idx;
    int dst_idx;
} CtxEdge;

typedef struct {
    int ctx_idx;
    int comp_idx;
} Anchor;

/* ============================================================
   Globals
   ============================================================ */

static State5 PATH_STATES[MAX_PATH];
static int PATH_LEN = 0;

static RCode ECODE[MAX_ECODE];
static int ECODE_LEN = 0;

static ParsePos PARSE_POS[MAX_PATH];
static int PARSE_POS_COUNT = 0;

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

static CompState COMP_STATES[MAX_COMP_STATES];
static int COMP_STATE_COUNT = 0;

static RawProfile RAW_PROFILES[MAX_PROFILES];
static int RAW_PROFILE_COUNT = 0;

static ObsEdge OBS_EDGES[MAX_OBS_EDGES];
static int OBS_EDGE_COUNT = 0;
static int OBS_ADJ[MAX_COMP_STATES][MAX_COMP_STATES];

static LocalCtx CTX[MAX_CONTEXTS];
static int CTX_COUNT = 0;

static CtxEdge PSI_EDGES[MAX_CTX_EDGES];
static int PSI_COUNT = 0;
static int PSI_ADJ[MAX_CONTEXTS][MAX_CONTEXTS];

static Anchor ANCHORS[MAX_ANCHORS];
static int ANCHOR_COUNT = 0;

static unsigned short DOMAIN[MAX_CONTEXTS];
static int FIRST_SOL[MAX_CONTEXTS];
static int SOLUTIONS = 0;

/* ============================================================
   Utility
   ============================================================ */

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

static char state_to_char(State5 s) {
    switch (s) {
        case A_ST: return 'A';
        case B_ST: return 'B';
        case C_ST: return 'C';
        case D_ST: return 'D';
        case E_ST: return 'E';
        default: return '?';
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
    const char *w = rcode_word(r);
    if (p1 < 1 || p1 > (int)strlen(w)) return INVALID_ST;
    return char_to_state(w[p1 - 1]);
}

static RCode token_to_rcode(const char *tok) {
    if (strcmp(tok, "R1") == 0) return R1;
    if (strcmp(tok, "R2") == 0) return R2;
    if (strcmp(tok, "R3") == 0) return R3;
    return INVALID_R;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || isspace((unsigned char)s[n-1]))) {
        s[--n] = '\0';
    }
}

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

/* ============================================================
   Load symbolic trace
   ============================================================ */

static void parse_path_line(const char *line) {
    const char *p = strchr(line, '{');
    if (!p) return;
    p++;

    while (*p && *p != '}') {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p || *p == '}') break;

        State5 s = char_to_state(*p);
        if (s != INVALID_ST) {
            if (PATH_LEN >= MAX_PATH) {
                fprintf(stderr, "PATH overflow\n");
                exit(1);
            }
            PATH_STATES[PATH_LEN++] = s;
        }

        while (*p && *p != ',' && *p != '}') p++;
    }
}

static void parse_ecode_line(const char *line) {
    const char *p = strchr(line, '{');
    if (!p) return;
    p++;

    while (*p && *p != '}') {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p || *p == '}') break;

        char tok[16];
        int k = 0;
        while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p) && k < 15) {
            tok[k++] = *p++;
        }
        tok[k] = '\0';

        RCode r = token_to_rcode(tok);
        if (r != INVALID_R) {
            if (ECODE_LEN >= MAX_ECODE) {
                fprintf(stderr, "ECODE overflow\n");
                exit(1);
            }
            ECODE[ECODE_LEN++] = r;
        }

        while (*p && *p != ',' && *p != '}') p++;
    }
}

static void load_symbolic_trace_txt(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open symbolic trace file %s\n", filename);
        exit(1);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strstr(line, "5-state path") != NULL) parse_path_line(line);
        if (strstr(line, "E-code") != NULL) parse_ecode_line(line);
    }

    fclose(fp);
}

/* ============================================================
   Build aligned parse positions
   ============================================================ */

static int try_build_parse_from_offset(int start_offset, int dry_run) {
    int path_i = start_offset;
    int parse_count = 0;

    for (int e_i = 0; e_i < ECODE_LEN; e_i++) {
        RCode r = ECODE[e_i];
        int len = rcode_len(r);

        for (int p = 1; p <= len; p++) {
            if (path_i >= PATH_LEN) return 0;

            State5 expected = rcode_letter(r, p);
            if (PATH_STATES[path_i] != expected) return 0;

            if (!dry_run) {
                PARSE_POS[parse_count].path_pos = path_i;
                PARSE_POS[parse_count].rho = r;
                PARSE_POS[parse_count].pos_in_rho = p;
                PARSE_POS[parse_count].ecode_index = e_i;
            }

            parse_count++;
            path_i++;
        }
    }

    if (!dry_run) PARSE_POS_COUNT = parse_count;
    return 1;
}

static int build_parse_positions(void) {
    for (int start = 0; start < PATH_LEN; start++) {
        if (PATH_STATES[start] != E_ST) continue;

        if (try_build_parse_from_offset(start, 1)) {
            try_build_parse_from_offset(start, 0);
            printf("Aligned symbolic parse at path index %d\n", start);
            return 1;
        }
    }

    return 0;
}

/* ============================================================
   Load observed profile CSV
   ============================================================ */

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
                   &rp.profile_id, &rp.len, rp.word,
                   &rp.weight, &rp.minpref, first, last) != 7) {
            continue;
        }

        rp.first = first[0];
        rp.last = last[0];
        rp.sid = sigclass_of_profile(rp.weight, rp.minpref, rp.first, rp.last);

        if (RAW_PROFILE_COUNT >= MAX_PROFILES) {
            fprintf(stderr, "Too many raw profiles\n");
            exit(1);
        }
        RAW_PROFILES[RAW_PROFILE_COUNT++] = rp;
    }

    fclose(fp);
}

static int raw_profile_sid(int pid) {
    for (int i = 0; i < RAW_PROFILE_COUNT; i++) {
        if (RAW_PROFILES[i].profile_id == pid) return RAW_PROFILES[i].sid;
    }
    return -1;
}

/* ============================================================
   Load observed edge CSV and compress
   ============================================================ */

static int comp_state_index(int sid, int debt) {
    for (int i = 0; i < COMP_STATE_COUNT; i++) {
        if (COMP_STATES[i].sid == sid && COMP_STATES[i].debt == debt) return i;
    }
    return -1;
}

static int add_comp_state_if_new(int sid, int debt) {
    int idx = comp_state_index(sid, debt);
    if (idx >= 0) return idx;

    if (COMP_STATE_COUNT >= MAX_COMP_STATES) {
        fprintf(stderr, "Too many compressed states\n");
        exit(1);
    }

    COMP_STATES[COMP_STATE_COUNT].sid = sid;
    COMP_STATES[COMP_STATE_COUNT].debt = debt;
    return COMP_STATE_COUNT++;
}

static void add_observed_edge_idx(int a, int b) {
    if (!OBS_ADJ[a][b]) {
        if (OBS_EDGE_COUNT >= MAX_OBS_EDGES) {
            fprintf(stderr, "Too many observed edges\n");
            exit(1);
        }
        OBS_ADJ[a][b] = 1;
        OBS_EDGES[OBS_EDGE_COUNT++] = (ObsEdge){a, b};
    }
}

static void load_edges_csv(const char *filename) {
    memset(OBS_ADJ, 0, sizeof(OBS_ADJ));

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

        int fsid = raw_profile_sid(fp_id);
        int tsid = raw_profile_sid(tp_id);
        if (fsid < 0 || tsid < 0) continue;

        int fidx = add_comp_state_if_new(fsid, fd);
        int tidx = add_comp_state_if_new(tsid, td);
        add_observed_edge_idx(fidx, tidx);
    }

    fclose(fp);
}

/* ============================================================
   Exact contexts from parsed trace
   ============================================================ */

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

static int admissible_ctx(const LocalCtx *c) {
    if (!(c->debt == 0 || c->debt == 2)) return 0;
    if (!(c->parity == 0 || c->parity == 1)) return 0;
    if (!allowed_edge(c->xm1, c->x0)) return 0;
    if (!allowed_edge(c->x0, c->xp1)) return 0;
    if (rcode_letter(c->rho, c->pos) != c->x0) return 0;
    return 1;
}

static void enumerate_admissible_contexts_from_parse(void) {
    for (int i = 0; i < PARSE_POS_COUNT; i++) {
        int p = PARSE_POS[i].path_pos;
        if (p <= 0 || p + 1 >= PATH_LEN) continue;

        for (int debt_i = 0; debt_i < 2; debt_i++) {
            LocalCtx c;
            c.xm1 = PATH_STATES[p - 1];
            c.x0  = PATH_STATES[p];
            c.xp1 = PATH_STATES[p + 1];
            c.rho = PARSE_POS[i].rho;
            c.pos = PARSE_POS[i].pos_in_rho;
            c.parity = p & 1;
            c.debt = (debt_i == 0 ? 0 : 2);

            if (admissible_ctx(&c)) add_ctx_if_new(&c);
        }
    }
}

/* ============================================================
   Exact Psi from parsed trace
   ============================================================ */

static void add_psi_edge(int s, int t) {
    if (!PSI_ADJ[s][t]) {
        if (PSI_COUNT >= MAX_CTX_EDGES) {
            fprintf(stderr, "Too many context edges\n");
            exit(1);
        }
        PSI_ADJ[s][t] = 1;
        PSI_EDGES[PSI_COUNT++] = (CtxEdge){s, t};
    }
}

static void build_psi_from_parse(void) {
    memset(PSI_ADJ, 0, sizeof(PSI_ADJ));
    PSI_COUNT = 0;

    for (int i = 0; i + 1 < PARSE_POS_COUNT; i++) {
        int p = PARSE_POS[i].path_pos;
        int q = PARSE_POS[i + 1].path_pos;

        if (p <= 0 || p + 1 >= PATH_LEN) continue;
        if (q <= 0 || q + 1 >= PATH_LEN) continue;

        for (int debt_i = 0; debt_i < 2; debt_i++) {
            int debt = (debt_i == 0 ? 0 : 2);

            LocalCtx a, b;

            a.xm1 = PATH_STATES[p - 1];
            a.x0  = PATH_STATES[p];
            a.xp1 = PATH_STATES[p + 1];
            a.rho = PARSE_POS[i].rho;
            a.pos = PARSE_POS[i].pos_in_rho;
            a.parity = p & 1;
            a.debt = debt;

            b.xm1 = PATH_STATES[q - 1];
            b.x0  = PATH_STATES[q];
            b.xp1 = PATH_STATES[q + 1];
            b.rho = PARSE_POS[i + 1].rho;
            b.pos = PARSE_POS[i + 1].pos_in_rho;
            b.parity = q & 1;
            b.debt = debt;

            if (!admissible_ctx(&a) || !admissible_ctx(&b)) continue;

            int ia = ctx_index(&a);
            int ib = ctx_index(&b);
            if (ia >= 0 && ib >= 0) add_psi_edge(ia, ib);
        }
    }
}

/* ============================================================
   Exact anchors
   exact_anchor_pairs.csv columns:
   macro_index,path_center_index,debt_before,profile_id
   ============================================================ */

static int anchor_exists(int ctx_idx, int comp_idx) {
    for (int i = 0; i < ANCHOR_COUNT; i++) {
        if (ANCHORS[i].ctx_idx == ctx_idx && ANCHORS[i].comp_idx == comp_idx) return 1;
    }
    return 0;
}

static void add_anchor(int ctx_idx, int comp_idx) {
    if (!anchor_exists(ctx_idx, comp_idx)) {
        if (ANCHOR_COUNT >= MAX_ANCHORS) {
            fprintf(stderr, "Too many anchors\n");
            exit(1);
        }
        ANCHORS[ANCHOR_COUNT++] = (Anchor){ctx_idx, comp_idx};
    }
}

static void load_exact_anchor_csv(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open anchor CSV %s\n", filename);
        exit(1);
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty anchor CSV\n");
        exit(1);
    }

    while (fgets(line, sizeof(line), fp)) {
        int macro_idx, path_center, debt_before, profile_id;
        if (sscanf(line, "%d,%d,%d,%d",
                   &macro_idx, &path_center, &debt_before, &profile_id) != 4) {
            continue;
        }

        (void)macro_idx;

        if (path_center <= 0 || path_center + 1 >= PATH_LEN) continue;
        if (!(debt_before == 0 || debt_before == 2)) continue;

        int sid = raw_profile_sid(profile_id);
        if (sid < 0) continue;

        int comp_idx = comp_state_index(sid, debt_before);
        if (comp_idx < 0) continue;

        int parse_i = -1;
        for (int k = 0; k < PARSE_POS_COUNT; k++) {
            if (PARSE_POS[k].path_pos == path_center) {
                parse_i = k;
                break;
            }
        }
        if (parse_i < 0) continue;

        LocalCtx c;
        c.xm1 = PATH_STATES[path_center - 1];
        c.x0  = PATH_STATES[path_center];
        c.xp1 = PATH_STATES[path_center + 1];
        c.rho = PARSE_POS[parse_i].rho;
        c.pos = PARSE_POS[parse_i].pos_in_rho;
        c.parity = path_center & 1;
        c.debt = debt_before;

        if (!admissible_ctx(&c)) continue;

        int idx = ctx_index(&c);
        if (idx >= 0) add_anchor(idx, comp_idx);
    }

    fclose(fp);
}

/* ============================================================
   CSP
   ============================================================ */

static int bitcount16(unsigned short x) {
    int c = 0;
    while (x) {
        c += (x & 1u);
        x >>= 1;
    }
    return c;
}

static int firstbit16(unsigned short x) {
    for (int i = 0; i < 16; i++) {
        if (x & (1u << i)) return i;
    }
    return -1;
}

static void init_domains(void) {
    for (int i = 0; i < CTX_COUNT; i++) {
        DOMAIN[i] = 0;
        for (int s = 0; s < COMP_STATE_COUNT; s++) {
            if (COMP_STATES[s].debt == CTX[i].debt) {
                DOMAIN[i] |= (1u << s);
            }
        }
    }

    for (int i = 0; i < ANCHOR_COUNT; i++) {
        DOMAIN[ANCHORS[i].ctx_idx] &= (1u << ANCHORS[i].comp_idx);
    }
}

static int propagate(unsigned short dom[MAX_CONTEXTS]) {
    int changed = 1;

    while (changed) {
        changed = 0;

        for (int i = 0; i < CTX_COUNT; i++) {
            if (dom[i] == 0) return 0;
        }

        for (int e = 0; e < PSI_COUNT; e++) {
            int u = PSI_EDGES[e].src_idx;
            int v = PSI_EDGES[e].dst_idx;

            unsigned short new_du = 0;
            for (int su = 0; su < COMP_STATE_COUNT; su++) {
                if (!(dom[u] & (1u << su))) continue;
                int ok = 0;
                for (int sv = 0; sv < COMP_STATE_COUNT; sv++) {
                    if (!(dom[v] & (1u << sv))) continue;
                    if (OBS_ADJ[su][sv]) {
                        ok = 1;
                        break;
                    }
                }
                if (ok) new_du |= (1u << su);
            }
            if (new_du != dom[u]) {
                dom[u] = new_du;
                changed = 1;
                if (dom[u] == 0) return 0;
            }

            unsigned short new_dv = 0;
            for (int sv = 0; sv < COMP_STATE_COUNT; sv++) {
                if (!(dom[v] & (1u << sv))) continue;
                int ok = 0;
                for (int su = 0; su < COMP_STATE_COUNT; su++) {
                    if (!(dom[u] & (1u << su))) continue;
                    if (OBS_ADJ[su][sv]) {
                        ok = 1;
                        break;
                    }
                }
                if (ok) new_dv |= (1u << sv);
            }
            if (new_dv != dom[v]) {
                dom[v] = new_dv;
                changed = 1;
                if (dom[v] == 0) return 0;
            }
        }
    }

    return 1;
}

static int choose_var(unsigned short dom[MAX_CONTEXTS]) {
    int best = -1;
    int best_sz = 999;

    for (int i = 0; i < CTX_COUNT; i++) {
        int sz = bitcount16(dom[i]);
        if (sz > 1 && sz < best_sz) {
            best = i;
            best_sz = sz;
        }
    }

    return best;
}

static void solve_rec(unsigned short dom[MAX_CONTEXTS]) {
    if (SOLUTIONS > 1) return;
    if (!propagate(dom)) return;

    int var = choose_var(dom);
    if (var < 0) {
        SOLUTIONS++;
        if (SOLUTIONS == 1) {
            for (int i = 0; i < CTX_COUNT; i++) {
                FIRST_SOL[i] = firstbit16(dom[i]);
            }
        }
        return;
    }

    unsigned short d = dom[var];
    for (int s = 0; s < COMP_STATE_COUNT; s++) {
        if (!(d & (1u << s))) continue;

        unsigned short dom2[MAX_CONTEXTS];
        memcpy(dom2, dom, sizeof(unsigned short) * CTX_COUNT);
        dom2[var] = (1u << s);
        solve_rec(dom2);

        if (SOLUTIONS > 1) return;
    }
}

static void print_comp_state(int idx) {
    printf("S%d[%d]", COMP_STATES[idx].sid, COMP_STATES[idx].debt);
}

/* ============================================================
   main
   ============================================================ */

int main(int argc, char **argv) {
    const char *profiles_csv = "D_macro_profiles_34.csv";
    const char *edges_csv = "D_debt_edges_34.csv";
    const char *symbolic_trace_txt = "symbolic_trace.txt";
    const char *anchor_csv = "exact_anchor_pairs.csv";

    if (argc >= 2) profiles_csv = argv[1];
    if (argc >= 3) edges_csv = argv[2];
    if (argc >= 4) symbolic_trace_txt = argv[3];
    if (argc >= 5) anchor_csv = argv[4];

    load_profiles_csv(profiles_csv);
    load_edges_csv(edges_csv);
    load_symbolic_trace_txt(symbolic_trace_txt);

    printf("Loaded path length: %d\n", PATH_LEN);
    printf("Loaded E-code length: %d\n", ECODE_LEN);

    if (!build_parse_positions()) {
        printf("RESULT: symbolic parse failed.\n");
        return 0;
    }

    enumerate_admissible_contexts_from_parse();
    build_psi_from_parse();
    load_exact_anchor_csv(anchor_csv);

    printf("Compressed observed states: %d\n", COMP_STATE_COUNT);
    printf("Compressed observed edges: %d\n", OBS_EDGE_COUNT);
    printf("Enumerated exact admissible contexts |Lambda_adm| = %d\n", CTX_COUNT);
    printf("Built exact local successor edges |Psi| = %d\n", PSI_COUNT);
    printf("Loaded exact anchors: %d\n", ANCHOR_COUNT);
    printf("Solving anchored exact pi ...\n\n");

    init_domains();
    solve_rec(DOMAIN);

    if (SOLUTIONS == 0) {
        printf("RESULT: no exact anchored pi exists.\n");
        return 0;
    }

    if (SOLUTIONS == 1) {
        printf("RESULT: unique exact anchored pi found.\n\n");
    } else {
        printf("RESULT: multiple exact anchored pi maps exist.\n");
        printf("Showing one exact anchored solution.\n\n");
    }

    for (int i = 0; i < CTX_COUNT; i++) {
        const LocalCtx *c = &CTX[i];
        printf("ctx %3d: (%c,%c,%c, %s, pos=%d, parity=%d, debt=%d) -> ",
               i,
               state_to_char(c->xm1),
               state_to_char(c->x0),
               state_to_char(c->xp1),
               rcode_name(c->rho),
               c->pos,
               c->parity,
               c->debt);
        print_comp_state(FIRST_SOL[i]);
        printf("\n");
    }

    return 0;
}