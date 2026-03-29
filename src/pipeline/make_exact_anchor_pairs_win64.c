#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 8192
#define MAX_PATH 1000000
#define MAX_ECODE 100000

typedef enum { A_ST=0, B_ST=1, C_ST=2, D_ST=3, E_ST=4, INVALID_ST=-1 } State5;
typedef enum { R1=0, R2=1, R3=2, INVALID_R=-1 } RCode;

typedef struct {
    int macro_index;
    int profile_id;
    int debt_before;
} MacroOcc;

typedef struct {
    int start_path_index;   /* 0-based start in 5-state path */
    int center_path_index;  /* 0-based anchor position */
    RCode rho;
    int ecode_index;
} CodeOcc;

/* ------------------------------------------------------------ */

static State5 PATH_STATES[MAX_PATH];
static int PATH_LEN = 0;

static RCode ECODE[MAX_ECODE];
static int ECODE_LEN = 0;

static CodeOcc OCCS[MAX_ECODE];
static int OCCS_LEN = 0;

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

static const char *rcode_word(RCode r) {
    switch (r) {
        case R1: return "EB";
        case R2: return "EADDDC";
        case R3: return "EADC";
        default: return "";
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

/* ------------------------------------------------------------ */

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
        s[--n] = '\0';
    }
}

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

    char line[8192];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strstr(line, "5-state path") != NULL) parse_path_line(line);
        if (strstr(line, "E-code") != NULL) parse_ecode_line(line);
    }

    fclose(fp);
}

/* ------------------------------------------------------------ */

static int anchor_offset(RCode r) {
    switch (r) {
        case R1: return 0; /* E in EB */
        case R2: return 2; /* first D in EADDDC */
        case R3: return 1; /* A in EADC */
        default: return 0;
    }
}

static int try_parse_from_offset(int start_offset, int dry_run) {
    int path_i = start_offset;
    int occ_count = 0;

    for (int e_i = 0; e_i < ECODE_LEN; e_i++) {
        RCode r = ECODE[e_i];
        int len = rcode_len(r);
        int start = path_i;

        for (int p = 1; p <= len; p++) {
            if (path_i >= PATH_LEN) return 0;

            State5 expected = rcode_letter(r, p);
            if (PATH_STATES[path_i] != expected) return 0;

            path_i++;
        }

        if (!dry_run) {
            OCCS[occ_count].start_path_index = start;
            OCCS[occ_count].center_path_index = start + anchor_offset(r);
            OCCS[occ_count].rho = r;
            OCCS[occ_count].ecode_index = e_i;
        }

        occ_count++;
    }

    if (!dry_run) OCCS_LEN = occ_count;
    return 1;
}

static void build_code_occurrences(void) {
    for (int start = 0; start < PATH_LEN; start++) {
        if (PATH_STATES[start] != E_ST) continue;

        if (try_parse_from_offset(start, 1)) {
            try_parse_from_offset(start, 0);
            printf("Aligned E-code parse at path index %d\n", start);
            return;
        }
    }

    fprintf(stderr, "Could not align E-code with 5-state path\n");
    exit(1);
}

/* ------------------------------------------------------------ */

static void write_exact_anchor_pairs_csv(const char *macro_occ_csv,
                                         const char *out_csv) {
    FILE *in = fopen(macro_occ_csv, "r");
    if (!in) {
        fprintf(stderr, "Cannot open macro occurrence CSV %s\n", macro_occ_csv);
        exit(1);
    }

    FILE *out = fopen(out_csv, "w");
    if (!out) {
        fprintf(stderr, "Cannot open output CSV %s\n", out_csv);
        fclose(in);
        exit(1);
    }

    fprintf(out, "macro_index,path_center_index,debt_before,profile_id\n");

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), in)) {
        fprintf(stderr, "Empty macro occurrence CSV\n");
        fclose(in);
        fclose(out);
        exit(1);
    }

    int written = 0;
    while (written < OCCS_LEN && fgets(line, sizeof(line), in)) {
        MacroOcc mo;
        if (sscanf(line, "%d,%d,%d",
                   &mo.macro_index, &mo.profile_id, &mo.debt_before) != 3) {
            continue;
        }

        fprintf(out, "%d,%d,%d,%d\n",
                mo.macro_index,
                OCCS[written].center_path_index,
                mo.debt_before,
                mo.profile_id);

        written++;
    }

    fclose(in);
    fclose(out);

    printf("Wrote %d anchor pairs to %s\n", written, out_csv);

    if (written < OCCS_LEN) {
        printf("Warning: symbolic trace has %d code occurrences, but only %d macro occurrences were available.\n",
               OCCS_LEN, written);
    }
}

/* ------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *symbolic_trace_txt = "symbolic_trace.txt";
    const char *macro_occ_csv = "macro_occurrences.csv";
    const char *out_csv = "exact_anchor_pairs.csv";

    if (argc >= 2) symbolic_trace_txt = argv[1];
    if (argc >= 3) macro_occ_csv = argv[2];
    if (argc >= 4) out_csv = argv[3];

    load_symbolic_trace_txt(symbolic_trace_txt);
    build_code_occurrences();

    printf("Loaded symbolic trace:\n");
    printf("  path length   = %d\n", PATH_LEN);
    printf("  E-code length = %d\n", ECODE_LEN);
    printf("  parsed code occurrences = %d\n", OCCS_LEN);

    write_exact_anchor_pairs_csv(macro_occ_csv, out_csv);

    return 0;
}
