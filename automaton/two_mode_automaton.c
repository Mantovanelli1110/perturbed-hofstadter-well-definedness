#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#define MAX_TRACE_PATH 1024
#define MAX_ECODE 256
#define MAX_COMP_STATES 16
#define MAX_CONTEXTS 256
#define MAX_CTX_EDGES 2048
#define MAX_COMPONENTS 64

#ifndef SEARCH_LIMIT_DEFAULT
#define SEARCH_LIMIT_DEFAULT 20000000ULL
#endif

typedef enum { A_ST=0, B_ST=1, C_ST=2, D_ST=3, E_ST=4, INVALID_ST=-1 } State5;
typedef enum { R1=0, R2=1, R3=2, INVALID_R=-1 } RCode;

typedef struct {
    int path_pos;
    RCode rho;
    int pos_in_rho;
    int ecode_index;
} ParsePos;

typedef struct {
    int sid;
    int debt;
} CompState;

typedef struct {
    int from_idx;
    int to_idx;
} ObsEdge;

typedef struct {
    State5 xm1, x0, xp1;
    RCode rho;
    int pos;
    int parity;
    int debt;
} LocalCtx;

typedef struct {
    int src_idx;
    int dst_idx;
} CtxEdge;

/* ------------------------------------------------------------ */
/* Embedded short symbolic segment                              */
/* ------------------------------------------------------------ */

static const char *EMBEDDED_SYMBOLIC_TRACE =
"Completed dyadic level m = 36\n"
"\n"
"5-state path = {D,D,C,E,B,E,A,D,D,D,C,E,B,E,B,E,A,D,C,E,A,D,C,E,B,E,A,D,D,D,D}\n"
"\n"
"E-code = {R1,R2,R1,R1,R3,R3,R1}\n";

/* ------------------------------------------------------------ */

static State5 PATH_STATES[MAX_TRACE_PATH];
static int PATH_LEN = 0;

static RCode ECODE[MAX_ECODE];
static int ECODE_LEN = 0;

static ParsePos PARSE_POS[MAX_TRACE_PATH];
static int PARSE_POS_COUNT = 0;

static CompState COMP_STATES[MAX_COMP_STATES];
static int COMP_STATE_COUNT = 0;

static ObsEdge OBS_EDGES[128];
static int OBS_EDGE_COUNT = 0;
static int OBS_ADJ[MAX_COMP_STATES][MAX_COMP_STATES];

static LocalCtx CTX[MAX_CONTEXTS];
static int CTX_COUNT = 0;

static CtxEdge PSI_EDGES[MAX_CTX_EDGES];
static int PSI_COUNT = 0;
static int PSI_ADJ[MAX_CONTEXTS][MAX_CONTEXTS];

static unsigned short DOMAIN[MAX_CONTEXTS];
static unsigned short SUPPORT[MAX_CONTEXTS];
static unsigned short SUPPORT_MODE_A[MAX_CONTEXTS];
static unsigned short SUPPORT_MODE_B[MAX_CONTEXTS];

static int COMP_OF_CTX[MAX_CONTEXTS];
static int COMP_SIZE[MAX_COMPONENTS];
static int COMPONENT_COUNT = 0;

static uint64_t SEARCH_LIMIT = SEARCH_LIMIT_DEFAULT;
static uint64_t SEARCH_NODES = 0;
static uint64_t SOLUTION_COUNT = 0;
static int HIT_LIMIT = 0;

/* ------------------------------------------------------------ */

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

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
        s[--n] = '\0';
    }
}

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

static void print_comp_state(int idx) {
    if (idx < 0) {
        printf("UNSET");
        return;
    }
    printf("S%d[%d]", COMP_STATES[idx].sid, COMP_STATES[idx].debt);
}

static void print_domain(unsigned short d) {
    int first = 1;
    for (int s = 0; s < COMP_STATE_COUNT; s++) {
        if (d & (unsigned short)(1u << s)) {
            if (!first) printf(" ");
            print_comp_state(s);
            first = 0;
        }
    }
    if (first) printf("EMPTY");
}

/* ------------------------------------------------------------ */
/* Parse embedded symbolic trace                                */
/* ------------------------------------------------------------ */

static void parse_path_line(const char *line) {
    const char *p = strchr(line, '{');
    if (!p) return;
    p++;

    while (*p && *p != '}') {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p || *p == '}') break;

        State5 s = char_to_state(*p);
        if (s != INVALID_ST) {
            if (PATH_LEN >= MAX_TRACE_PATH) {
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

        {
            RCode r = token_to_rcode(tok);
            if (r != INVALID_R) ECODE[ECODE_LEN++] = r;
        }

        while (*p && *p != ',' && *p != '}') p++;
    }
}

static void load_symbolic_trace_from_embedded_string(void) {
    size_t n = strlen(EMBEDDED_SYMBOLIC_TRACE);
    char *buf = (char *)malloc(n + 1);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    memcpy(buf, EMBEDDED_SYMBOLIC_TRACE, n + 1);

    {
        char *line = strtok(buf, "\n");
        while (line) {
            char tmp[8192];
            snprintf(tmp, sizeof(tmp), "%s", line);
            trim(tmp);
            if (strstr(tmp, "5-state path") != NULL) parse_path_line(tmp);
            if (strstr(tmp, "E-code") != NULL) parse_ecode_line(tmp);
            line = strtok(NULL, "\n");
        }
    }

    free(buf);
}

static int try_build_parse_from_offset(int start_offset, int dry_run) {
    int path_i = start_offset;
    int parse_count = 0;

    for (int e_i = 0; e_i < ECODE_LEN; e_i++) {
        RCode r = ECODE[e_i];
        int len = rcode_len(r);

        for (int p = 1; p <= len; p++) {
            if (path_i >= PATH_LEN) return 0;
            {
                State5 expected = rcode_letter(r, p);
                if (PATH_STATES[path_i] != expected) return 0;
            }

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

/* ------------------------------------------------------------ */
/* Real stabilized 9-state automaton                            */
/* ------------------------------------------------------------ */

static int add_comp_state_if_new(int sid, int debt) {
    for (int i = 0; i < COMP_STATE_COUNT; i++) {
        if (COMP_STATES[i].sid == sid && COMP_STATES[i].debt == debt) return i;
    }
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
        OBS_ADJ[a][b] = 1;
        OBS_EDGES[OBS_EDGE_COUNT++] = (ObsEdge){a, b};
    }
}

static void build_real_stabilized_automaton(void) {
    memset(OBS_ADJ, 0, sizeof(OBS_ADJ));
    COMP_STATE_COUNT = 0;
    OBS_EDGE_COUNT = 0;

    add_comp_state_if_new(0, 2);
    add_comp_state_if_new(1, 0);
    add_comp_state_if_new(2, 0);
    add_comp_state_if_new(3, 0);
    add_comp_state_if_new(4, 0);
    add_comp_state_if_new(5, 0);
    add_comp_state_if_new(6, 0);
    add_comp_state_if_new(6, 2);
    add_comp_state_if_new(7, 0);

    static const int REAL_EDGES[][4] = {
        {3,0,6,0},{7,0,4,0},{4,0,5,0},{5,0,5,0},{4,0,2,0},{2,0,6,0},
        {6,0,6,0},{6,0,7,0},{7,0,1,0},{1,0,1,0},{0,2,7,0},{7,0,0,2},
        {1,0,4,0},{4,0,4,0},{2,0,4,0},{2,0,3,0},{3,0,5,0},{5,0,4,0},
        {3,0,3,0},{3,0,4,0},{4,0,3,0},{5,0,2,0},{1,0,0,2},{0,2,6,2},
        {6,2,7,0},{3,0,2,0},{2,0,2,0},{2,0,5,0},{5,0,3,0},{6,2,6,2},
        {6,0,3,0},{4,0,6,0},{6,0,5,0},{4,0,1,0},{4,0,0,2},{5,0,6,0},
        {6,0,2,0},
    };

    {
        int m = (int)(sizeof(REAL_EDGES) / sizeof(REAL_EDGES[0]));
        for (int i = 0; i < m; i++) {
            int fsid = REAL_EDGES[i][0];
            int fdebt = REAL_EDGES[i][1];
            int tsid = REAL_EDGES[i][2];
            int tdebt = REAL_EDGES[i][3];

            int a = -1, b = -1;
            for (int u = 0; u < COMP_STATE_COUNT; u++) {
                if (COMP_STATES[u].sid == fsid && COMP_STATES[u].debt == fdebt) { a = u; break; }
            }
            for (int v = 0; v < COMP_STATE_COUNT; v++) {
                if (COMP_STATES[v].sid == tsid && COMP_STATES[v].debt == tdebt) { b = v; break; }
            }
            if (a < 0 || b < 0) {
                fprintf(stderr, "Bad hardcoded edge\n");
                exit(1);
            }
            add_observed_edge_idx(a, b);
        }
    }
}

/* ------------------------------------------------------------ */
/* Exact local contexts and symbolic edges                      */
/* ------------------------------------------------------------ */

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
    CTX_COUNT = 0;
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

static void add_psi_edge(int s, int t) {
    if (!PSI_ADJ[s][t]) {
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

            {
                int ia = ctx_index(&a);
                int ib = ctx_index(&b);
                if (ia >= 0 && ib >= 0) add_psi_edge(ia, ib);
            }
        }
    }
}

/* ------------------------------------------------------------ */
/* Domain setup and propagation                                 */
/* ------------------------------------------------------------ */

static void init_domains_all_debt_matching(void) {
    for (int i = 0; i < CTX_COUNT; i++) {
        DOMAIN[i] = 0;
        for (int s = 0; s < COMP_STATE_COUNT; s++) {
            if (COMP_STATES[s].debt == CTX[i].debt) {
                DOMAIN[i] |= (unsigned short)(1u << s);
            }
        }
    }
}

static void force_debt2_to_S6_2(void) {
    int target = -1;
    for (int s = 0; s < COMP_STATE_COUNT; s++) {
        if (COMP_STATES[s].sid == 6 && COMP_STATES[s].debt == 2) {
            target = s;
            break;
        }
    }
    if (target < 0) {
        fprintf(stderr, "Could not find S6[2]\n");
        exit(1);
    }

    for (int i = 0; i < CTX_COUNT; i++) {
        if (CTX[i].debt == 2) {
            DOMAIN[i] = (unsigned short)(1u << target);
        }
    }
}

static int candidate_has_out_support(int i, int s, const unsigned short dom[]) {
    for (int j = 0; j < CTX_COUNT; j++) {
        if (!PSI_ADJ[i][j]) continue;

        int ok = 0;
        for (int t = 0; t < COMP_STATE_COUNT; t++) {
            if (!(dom[j] & (unsigned short)(1u << t))) continue;
            if (OBS_ADJ[s][t]) { ok = 1; break; }
        }
        if (!ok) return 0;
    }
    return 1;
}

static int candidate_has_in_support(int i, int s, const unsigned short dom[]) {
    for (int j = 0; j < CTX_COUNT; j++) {
        if (!PSI_ADJ[j][i]) continue;

        int ok = 0;
        for (int t = 0; t < COMP_STATE_COUNT; t++) {
            if (!(dom[j] & (unsigned short)(1u << t))) continue;
            if (OBS_ADJ[t][s]) { ok = 1; break; }
        }
        if (!ok) return 0;
    }
    return 1;
}

static int propagate_domains(unsigned short dom[]) {
    int changed = 1;

    while (changed) {
        changed = 0;

        for (int i = 0; i < CTX_COUNT; i++) {
            unsigned short newd = 0;

            for (int s = 0; s < COMP_STATE_COUNT; s++) {
                if (!(dom[i] & (unsigned short)(1u << s))) continue;
                if (!candidate_has_out_support(i, s, dom)) continue;
                if (!candidate_has_in_support(i, s, dom)) continue;
                newd |= (unsigned short)(1u << s);
            }

            if (newd != dom[i]) {
                dom[i] = newd;
                changed = 1;
            }

            if (dom[i] == 0) return 0;
        }
    }

    return 1;
}

/* ------------------------------------------------------------ */
/* Components                                                   */
/* ------------------------------------------------------------ */

static void build_connected_components(void) {
    COMPONENT_COUNT = 0;
    for (int i = 0; i < CTX_COUNT; i++) COMP_OF_CTX[i] = -1;

    int queue[MAX_CONTEXTS];

    for (int s = 0; s < CTX_COUNT; s++) {
        if (COMP_OF_CTX[s] != -1) continue;

        int head = 0, tail = 0;
        queue[tail++] = s;
        COMP_OF_CTX[s] = COMPONENT_COUNT;
        COMP_SIZE[COMPONENT_COUNT] = 0;

        while (head < tail) {
            int u = queue[head++];
            COMP_SIZE[COMPONENT_COUNT]++;

            for (int v = 0; v < CTX_COUNT; v++) {
                if ((PSI_ADJ[u][v] || PSI_ADJ[v][u]) && COMP_OF_CTX[v] == -1) {
                    COMP_OF_CTX[v] = COMPONENT_COUNT;
                    queue[tail++] = v;
                }
            }
        }

        COMPONENT_COUNT++;
        if (COMPONENT_COUNT >= MAX_COMPONENTS) {
            fprintf(stderr, "Too many components\n");
            exit(1);
        }
    }
}

static int ctx_in_component(int ctx, int comp_id) {
    return COMP_OF_CTX[ctx] == comp_id;
}

/* ------------------------------------------------------------ */
/* Search                                                       */
/* ------------------------------------------------------------ */

static int choose_var_component(const unsigned short dom[], int comp_id) {
    int best = -1;
    int best_sz = 999;

    for (int i = 0; i < CTX_COUNT; i++) {
        if (!ctx_in_component(i, comp_id)) continue;
        if (CTX[i].debt != 0) continue;

        {
            int sz = bitcount16(dom[i]);
            if (sz > 1 && sz < best_sz) {
                best = i;
                best_sz = sz;
            }
        }
    }
    return best;
}

static void record_solution_support(const unsigned short dom[], int comp_id) {
    for (int i = 0; i < CTX_COUNT; i++) {
        if (!ctx_in_component(i, comp_id)) continue;
        SUPPORT[i] |= dom[i];
    }
}

static void search_component(unsigned short dom[], int comp_id) {
    if (HIT_LIMIT) return;

    SEARCH_NODES++;
    if (SEARCH_NODES > SEARCH_LIMIT) {
        HIT_LIMIT = 1;
        return;
    }

    if (!propagate_domains(dom)) return;

    {
        int var = choose_var_component(dom, comp_id);
        if (var < 0) {
            SOLUTION_COUNT++;
            record_solution_support(dom, comp_id);
            return;
        }

        {
            unsigned short d = dom[var];
            for (int s = 0; s < COMP_STATE_COUNT; s++) {
                if (!(d & (unsigned short)(1u << s))) continue;

                unsigned short dom2[MAX_CONTEXTS];
                memcpy(dom2, dom, sizeof(unsigned short) * (size_t)CTX_COUNT);
                dom2[var] = (unsigned short)(1u << s);
                search_component(dom2, comp_id);

                if (HIT_LIMIT) return;
            }
        }
    }
}

/* ------------------------------------------------------------ */

static void print_domains(const char *title, const unsigned short dom[]) {
    printf("%s\n", title);
    for (int i = 0; i < CTX_COUNT; i++) {
        printf("ctx %2d: (%c,%c,%c,%s,pos=%d,parity=%d,debt=%d)\n",
               i,
               state_to_char(CTX[i].xm1),
               state_to_char(CTX[i].x0),
               state_to_char(CTX[i].xp1),
               rcode_name(CTX[i].rho),
               CTX[i].pos,
               CTX[i].parity,
               CTX[i].debt);
        printf("    domain size = %d : ", bitcount16(dom[i]));
        print_domain(dom[i]);
        printf("\n");
    }
    printf("\n");
}

static int find_state_index(int sid, int debt) {
    for (int i = 0; i < COMP_STATE_COUNT; i++) {
        if (COMP_STATES[i].sid == sid && COMP_STATES[i].debt == debt) return i;
    }
    return -1;
}

static void run_mode(const char *mode_name, int ctx_id, int state_idx, unsigned short out_support[]) {
    unsigned short dom[MAX_CONTEXTS];

    memcpy(dom, DOMAIN, sizeof(unsigned short) * (size_t)CTX_COUNT);
    dom[ctx_id] = (unsigned short)(1u << state_idx);

    printf("\n==============================\n");
    printf("Mode %s: fixing ctx %d -> ", mode_name, ctx_id);
    print_comp_state(state_idx);
    printf("\n==============================\n");

    if (!propagate_domains(dom)) {
        printf("Immediate contradiction after fixing.\n");
        memset(out_support, 0, sizeof(unsigned short) * (size_t)CTX_COUNT);
        return;
    }

    print_domains("Domains after fixing mode root and propagating:", dom);

    memset(SUPPORT, 0, sizeof(SUPPORT));

    for (int c = 0; c < COMPONENT_COUNT; c++) {
        unsigned short dom2[MAX_CONTEXTS];
        memcpy(dom2, dom, sizeof(unsigned short) * (size_t)CTX_COUNT);

        SEARCH_NODES = 0;
        SOLUTION_COUNT = 0;
        HIT_LIMIT = 0;

        search_component(dom2, c);

        printf("component %d: size=%d, nodes=%" PRIu64 ", solutions=%" PRIu64,
               c, COMP_SIZE[c], SEARCH_NODES, SOLUTION_COUNT);
        if (HIT_LIMIT) printf(" (limit hit)");
        printf("\n");
    }

    printf("\nSupport sets in mode %s:\n", mode_name);
    for (int i = 0; i < CTX_COUNT; i++) {
        printf("ctx %2d: (%c,%c,%c,%s,pos=%d,parity=%d,debt=%d) -> ",
               i,
               state_to_char(CTX[i].xm1),
               state_to_char(CTX[i].x0),
               state_to_char(CTX[i].xp1),
               rcode_name(CTX[i].rho),
               CTX[i].pos,
               CTX[i].parity,
               CTX[i].debt);
        print_domain(SUPPORT[i]);
        printf("\n");
    }
    printf("\n");

    memcpy(out_support, SUPPORT, sizeof(unsigned short) * (size_t)CTX_COUNT);
}

static void print_two_mode_table(void) {
    printf("\n========================================================\n");
    printf("2-mode automaton support table\n");
    printf("Mode A = root class S1[0], Mode B = root class S2[0]\n");
    printf("========================================================\n");

    for (int i = 0; i < CTX_COUNT; i++) {
        unsigned short inter  = (unsigned short)(SUPPORT_MODE_A[i] & SUPPORT_MODE_B[i]);
        unsigned short only_a = (unsigned short)(SUPPORT_MODE_A[i] & (unsigned short)(~SUPPORT_MODE_B[i]));
        unsigned short only_b = (unsigned short)(SUPPORT_MODE_B[i] & (unsigned short)(~SUPPORT_MODE_A[i]));

        printf("ctx %2d: (%c,%c,%c,%s,pos=%d,parity=%d,debt=%d)\n",
               i,
               state_to_char(CTX[i].xm1),
               state_to_char(CTX[i].x0),
               state_to_char(CTX[i].xp1),
               rcode_name(CTX[i].rho),
               CTX[i].pos,
               CTX[i].parity,
               CTX[i].debt);

        printf("    Mode A support : ");
        print_domain(SUPPORT_MODE_A[i]);
        printf("\n");

        printf("    Mode B support : ");
        print_domain(SUPPORT_MODE_B[i]);
        printf("\n");

        printf("    Common support : ");
        print_domain(inter);
        printf("\n");

        printf("    A-only support : ");
        print_domain(only_a);
        printf("\n");

        printf("    B-only support : ");
        print_domain(only_b);
        printf("\n\n");
    }
}

/* ------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc >= 2) {
        char *endptr = NULL;
        uint64_t x = strtoull(argv[1], &endptr, 10);
        if (endptr && *endptr == '\0' && x > 0) SEARCH_LIMIT = x;
    }

    load_symbolic_trace_from_embedded_string();

    printf("Loaded path length: %d\n", PATH_LEN);
    printf("Loaded E-code length: %d\n", ECODE_LEN);

    if (!build_parse_positions()) {
        printf("RESULT: symbolic parse failed.\n");
        return 0;
    }

    build_real_stabilized_automaton();
    enumerate_admissible_contexts_from_parse();
    build_psi_from_parse();
    build_connected_components();

    printf("Observed real compressed states: %d\n", COMP_STATE_COUNT);
    printf("Observed real compressed edges: %d\n", OBS_EDGE_COUNT);
    printf("Enumerated exact admissible contexts |Lambda_adm| = %d\n", CTX_COUNT);
    printf("Built exact local successor edges |Psi| = %d\n", PSI_COUNT);
    printf("Connected components: %d\n", COMPONENT_COUNT);
    printf("Search limit per component: %" PRIu64 "\n\n", SEARCH_LIMIT);

    init_domains_all_debt_matching();
    force_debt2_to_S6_2();

    if (!propagate_domains(DOMAIN)) {
        printf("RESULT: inconsistency detected before mode split.\n");
        return 0;
    }

    print_domains("Domains after forcing debt-2 = S6[2] and propagation:", DOMAIN);

    {
        int ctx0 = 0;
        int s1 = find_state_index(1, 0);
        int s2 = find_state_index(2, 0);

        if (s1 < 0 || s2 < 0) {
            fprintf(stderr, "Could not find S1[0] or S2[0]\n");
            return 1;
        }

        run_mode("A", ctx0, s1, SUPPORT_MODE_A);
        run_mode("B", ctx0, s2, SUPPORT_MODE_B);
        print_two_mode_table();
    }

    printf("RESULT: 2-mode automaton extracted.\n");
    return 0;
}