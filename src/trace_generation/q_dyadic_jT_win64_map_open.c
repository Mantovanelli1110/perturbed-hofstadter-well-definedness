#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <windows.h>

#ifndef DEFAULT_MAX_M
#define DEFAULT_MAX_M 36
#endif

typedef uint64_t QTYPE;

/* ---------- utility ---------- */

static int is_power_of_two_u64(uint64_t x) {
    return x && ((x & (x - 1)) == 0);
}

static int floor_log2_u64(uint64_t x) {
    int r = -1;
    while (x) {
        x >>= 1;
        r++;
    }
    return r;
}

static void die_last_error(const char *msg) {
    DWORD e = GetLastError();
    fprintf(stderr, "%s (GetLastError=%lu)\n", msg, (unsigned long)e);
    exit(1);
}

/* ---------- dynamic vectors ---------- */

typedef struct {
    int64_t *a;
    size_t n, cap;
} VecI64;

typedef struct {
    int *a;
    size_t n, cap;
} VecI;

typedef struct {
    char *a;
    size_t n, cap;
} VecC;

static void vec_i64_push(VecI64 *v, int64_t x) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 64;
        v->a = (int64_t *)realloc(v->a, v->cap * sizeof(int64_t));
        if (!v->a) { perror("realloc VecI64"); exit(1); }
    }
    v->a[v->n++] = x;
}

static void vec_i_push(VecI *v, int x) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 64;
        v->a = (int *)realloc(v->a, v->cap * sizeof(int));
        if (!v->a) { perror("realloc VecI"); exit(1); }
    }
    v->a[v->n++] = x;
}

static void vec_c_push(VecC *v, char x) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 64;
        v->a = (char *)realloc(v->a, v->cap * sizeof(char));
        if (!v->a) { perror("realloc VecC"); exit(1); }
    }
    v->a[v->n++] = x;
}

/* ---------- state map from 4-blocks ---------- */

static char state_from_block4(int b4) {
    switch (b4) {
        case 0b0010: case 0b1101: return 'A';
        case 0b0011: case 0b1100: return 'B';
        case 0b0100: case 0b1011: return 'C';
        case 0b0101: case 0b1010: return 'D';
        case 0b0110: case 0b1001: return 'E';
        default: return '?';
    }
}

/* ---------- pretty-print ---------- */

static void print_state_word(const char *w, size_t len) {
    printf("{");
    for (size_t i = 0; i < len; i++) {
        printf("%c", w[i]);
        if (i + 1 < len) printf(",");
    }
    printf("}");
}

static void print_int_vec_inline(const int *a, size_t n) {
    printf("{");
    for (size_t i = 0; i < n; i++) {
        printf("%d", a[i]);
        if (i + 1 < n) printf(",");
    }
    printf("}");
}

static void print_i64_vec_inline(const int64_t *a, size_t n) {
    printf("{");
    for (size_t i = 0; i < n; i++) {
        printf("%" PRId64, a[i]);
        if (i + 1 < n) printf(",");
    }
    printf("}");
}

static void print_char_vec_inline(const char *a, size_t n) {
    printf("{");
    for (size_t i = 0; i < n; i++) {
        printf("%c", a[i]);
        if (i + 1 < n) printf(",");
    }
    printf("}");
}

/* ---------- word lists ---------- */

typedef struct {
    char *data;
    size_t *start;
    size_t *len;
    size_t n, cap;
    size_t data_n, data_cap;
} WordList;

static void wordlist_push(WordList *wl, const char *w, size_t len) {
    if (wl->n == wl->cap) {
        wl->cap = wl->cap ? 2 * wl->cap : 32;
        wl->start = (size_t *)realloc(wl->start, wl->cap * sizeof(size_t));
        wl->len   = (size_t *)realloc(wl->len,   wl->cap * sizeof(size_t));
        if (!wl->start || !wl->len) { perror("realloc WordList meta"); exit(1); }
    }
    if (wl->data_n + len > wl->data_cap) {
        while (wl->data_n + len > wl->data_cap) wl->data_cap = wl->data_cap ? 2 * wl->data_cap : 256;
        wl->data = (char *)realloc(wl->data, wl->data_cap * sizeof(char));
        if (!wl->data) { perror("realloc WordList data"); exit(1); }
    }
    wl->start[wl->n] = wl->data_n;
    wl->len[wl->n] = len;
    memcpy(wl->data + wl->data_n, w, len);
    wl->data_n += len;
    wl->n++;
}

static const char *wordlist_word(const WordList *wl, size_t i) {
    return wl->data + wl->start[i];
}

static void wordlist_free(WordList *wl) {
    free(wl->data);
    free(wl->start);
    free(wl->len);
    memset(wl, 0, sizeof(*wl));
}

/* ---------- return words ---------- */

static void extract_return_words(const char *path, size_t n, char symbol, WordList *out) {
    for (size_t i = 0; i < n; i++) {
        if (path[i] != symbol) continue;

        size_t j = i + 1;
        while (j < n && path[j] != symbol) j++;

        /* only complete return words */
        if (j < n) {
            wordlist_push(out, path + i, j - i);
            i = j - 1;
        } else {
            break;
        }
    }
}

/* ---------- D words -> j ---------- */

static int is_nontrivial_D_word(const char *w, size_t len) {
    return !(len == 1 && w[0] == 'D');
}

static int j_from_D_word(const char *w, size_t len, int *ok) {
    *ok = 0;
    if (len < 4) return -1;
    if (!(w[0] == 'D' && w[1] == 'C' && w[2] == 'E' && w[len - 1] == 'A')) return -1;
    if (((int)len - 4) % 2 != 0) return -1;
    int j = ((int)len - 4) / 2;
    for (int k = 0; k < j; k++) {
        if (w[3 + 2*k] != 'B' || w[3 + 2*k + 1] != 'E') return -1;
    }
    *ok = 1;
    return j;
}

/* ---------- E words -> codes ---------- */

static const char *rcode_from_E_word(const char *w, size_t len) {
    if (len == 2 && w[0] == 'E' && w[1] == 'B') return "R1";
    if (len == 6 && w[0]=='E' && w[1]=='A' && w[2]=='D' && w[3]=='D' && w[4]=='D' && w[5]=='C') return "R2";
    if (len == 4 && w[0]=='E' && w[1]=='A' && w[2]=='D' && w[3]=='C') return "R3";
    return "UNK";
}

typedef struct {
    const char **rcode;
    size_t n;
} ECodeSeq;

static ECodeSeq build_ecode(const WordList *ewords) {
    ECodeSeq out;
    out.n = ewords->n;
    out.rcode = (const char **)malloc(out.n * sizeof(const char *));
    if (!out.rcode) { perror("malloc ECodeSeq"); exit(1); }
    for (size_t i = 0; i < out.n; i++) {
        out.rcode[i] = rcode_from_E_word(wordlist_word(ewords, i), ewords->len[i]);
    }
    return out;
}

static void free_ecode(ECodeSeq *ec) {
    free(ec->rcode);
    ec->rcode = NULL;
    ec->n = 0;
}

/* ---------- annotated runs ---------- */

typedef struct {
    int runlen;
    const char *terminal;
} AnnotRun;

typedef struct {
    AnnotRun *a;
    size_t n, cap;
} AnnotRuns;

static void annotruns_push(AnnotRuns *v, int runlen, const char *terminal) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 16;
        v->a = (AnnotRun *)realloc(v->a, v->cap * sizeof(AnnotRun));
        if (!v->a) { perror("realloc AnnotRuns"); exit(1); }
    }
    v->a[v->n].runlen = runlen;
    v->a[v->n].terminal = terminal;
    v->n++;
}

static AnnotRuns build_annotated_runs(const ECodeSeq *ec) {
    AnnotRuns out = {0};
    int run = 0;
    for (size_t i = 0; i < ec->n; i++) {
        if (strcmp(ec->rcode[i], "R1") == 0) {
            run++;
        } else {
            if (strcmp(ec->rcode[i], "UNK") != 0) {
                annotruns_push(&out, run, ec->rcode[i]);
            }
            run = 0;
        }
    }
    return out;
}

static void free_annotruns(AnnotRuns *ar) {
    free(ar->a);
    ar->a = NULL;
    ar->n = ar->cap = 0;
}

/* ---------- open right E-suffix ---------- */

typedef struct {
    int exists;
    int open_runlen;
    char *suffix;
    size_t suffix_len;
    char *remainder;
    size_t remainder_len;
} OpenESuffix;

static void free_open_e_suffix(OpenESuffix *o) {
    free(o->suffix);
    free(o->remainder);
    o->suffix = NULL;
    o->remainder = NULL;
    o->suffix_len = 0;
    o->remainder_len = 0;
    o->open_runlen = 0;
    o->exists = 0;
}

static OpenESuffix extract_open_e_suffix(const char *path, size_t n) {
    OpenESuffix out;
    memset(&out, 0, sizeof(out));

    if (n == 0) return out;

    ssize_t lastE = -1;
    for (size_t i = 0; i < n; i++) {
        if (path[i] == 'E') lastE = (ssize_t)i;
    }
    if (lastE < 0) return out;

    out.exists = 1;
    out.suffix_len = n - (size_t)lastE;
    out.suffix = (char *)malloc(out.suffix_len * sizeof(char));
    if (!out.suffix) {
        perror("malloc open suffix");
        exit(1);
    }
    memcpy(out.suffix, path + lastE, out.suffix_len);

    size_t pos = 0;
    int k = 0;
    while (pos + 1 < out.suffix_len &&
           out.suffix[pos] == 'E' &&
           out.suffix[pos + 1] == 'B') {
        k++;
        pos += 2;
    }
    out.open_runlen = k;

    out.remainder_len = out.suffix_len - pos;
    out.remainder = (char *)malloc(out.remainder_len * sizeof(char));
    if (!out.remainder) {
        perror("malloc open remainder");
        exit(1);
    }
    memcpy(out.remainder, out.suffix + pos, out.remainder_len);

    return out;
}

/* ---------- CSV ---------- */

static void write_csv_header(FILE *csv) {
    fprintf(csv, "kind,level,idx_in_level,j,T,j_next,T_next,open_runlen,open_suffix,open_remainder\n");
}

static void write_transition_csv(FILE *csv, int level, size_t idx, int j, const char *T, int jn, const char *Tn) {
    fprintf(csv, "transition,%d,%zu,%d,%s,%d,%s,,,\n", level, idx, j, T, jn, Tn);
}

static void write_open_suffix_csv(FILE *csv, int level, const OpenESuffix *o) {
    fprintf(csv, "open,%d,,,,,,%d,", level, o->open_runlen);

    fprintf(csv, "\"");
    for (size_t i = 0; i < o->suffix_len; i++) fprintf(csv, "%c", o->suffix[i]);
    fprintf(csv, "\",");

    fprintf(csv, "\"");
    for (size_t i = 0; i < o->remainder_len; i++) fprintf(csv, "%c", o->remainder[i]);
    fprintf(csv, "\"\n");
}

/* ---------- Windows file mapping ---------- */

typedef struct {
    HANDLE hFile;
    HANDLE hMap;
    QTYPE *ptr;
    uint64_t entries;
} FileMappedArray;

static FileMappedArray fmap_create_u64(const char *filename, uint64_t entries) {
    FileMappedArray fm;
    memset(&fm, 0, sizeof(fm));
    fm.entries = entries;

    uint64_t bytes = entries * (uint64_t)sizeof(QTYPE);
    DWORD sizeLow = (DWORD)(bytes & 0xFFFFFFFFULL);
    DWORD sizeHigh = (DWORD)(bytes >> 32);

    fm.hFile = CreateFileA(
        filename,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (fm.hFile == INVALID_HANDLE_VALUE) {
        die_last_error("CreateFileA failed");
    }

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)bytes;
    if (!SetFilePointerEx(fm.hFile, li, NULL, FILE_BEGIN)) {
        die_last_error("SetFilePointerEx failed");
    }
    if (!SetEndOfFile(fm.hFile)) {
        die_last_error("SetEndOfFile failed");
    }

    fm.hMap = CreateFileMappingA(
        fm.hFile,
        NULL,
        PAGE_READWRITE,
        sizeHigh,
        sizeLow,
        NULL
    );
    if (!fm.hMap) {
        die_last_error("CreateFileMappingA failed");
    }

    fm.ptr = (QTYPE *)MapViewOfFile(
        fm.hMap,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        (SIZE_T)bytes
    );
    if (!fm.ptr) {
        die_last_error("MapViewOfFile failed");
    }

    return fm;
}

static void fmap_close(FileMappedArray *fm) {
    if (fm->ptr) UnmapViewOfFile(fm->ptr);
    if (fm->hMap) CloseHandle(fm->hMap);
    if (fm->hFile && fm->hFile != INVALID_HANDLE_VALUE) CloseHandle(fm->hFile);
    fm->ptr = NULL;
    fm->hMap = NULL;
    fm->hFile = NULL;
}

/* ---------- analysis ---------- */

static void analyze_level(
    int m,
    const VecI64 *Ehist, const VecI64 *Fhist, const VecI64 *Ghist,
    const VecI64 *Khist, const VecI64 *Deltahist,
    const VecI *cseq, const VecI *dseq,
    const VecC *states,
    FILE *csv
) {
    printf("Completed dyadic level m = %d\n\n", m);

    printf("E = "); print_i64_vec_inline(Ehist->a, Ehist->n); printf("\n\n");
    printf("F = "); print_i64_vec_inline(Fhist->a, Fhist->n); printf("\n\n");
    printf("G = "); print_i64_vec_inline(Ghist->a, Ghist->n); printf("\n\n");
    printf("kappa = "); print_i64_vec_inline(Khist->a, Khist->n); printf("\n\n");
    printf("delta = "); print_i64_vec_inline(Deltahist->a, Deltahist->n); printf("\n\n");
    printf("c = "); print_int_vec_inline(cseq->a, cseq->n); printf("\n\n");
    printf("d = "); print_int_vec_inline(dseq->a, dseq->n); printf("\n\n");

    printf("5-state path = ");
    print_char_vec_inline(states->a, states->n);
    printf("\n\n");

    WordList dwords = {0}, ewords = {0};
    extract_return_words(states->a, states->n, 'D', &dwords);
    extract_return_words(states->a, states->n, 'E', &ewords);

    printf("D-return words = {");
    for (size_t i = 0; i < dwords.n; i++) {
        print_state_word(wordlist_word(&dwords, i), dwords.len[i]);
        if (i + 1 < dwords.n) printf(",");
    }
    printf("}\n\n");

    printf("E-return words = {");
    for (size_t i = 0; i < ewords.n; i++) {
        print_state_word(wordlist_word(&ewords, i), ewords.len[i]);
        if (i + 1 < ewords.n) printf(",");
    }
    printf("}\n");

    VecI nontriv_j = {0};
    for (size_t i = 0; i < dwords.n; i++) {
        const char *w = wordlist_word(&dwords, i);
        size_t len = dwords.len[i];
        if (is_nontrivial_D_word(w, len)) {
            int ok = 0;
            int j = j_from_D_word(w, len, &ok);
            if (ok) vec_i_push(&nontriv_j, j);
        }
    }

    printf("nontrivial j_m = ");
    print_int_vec_inline(nontriv_j.a, nontriv_j.n);
    printf("\n");

    ECodeSeq ec = build_ecode(&ewords);

    printf("E-code = {");
    for (size_t i = 0; i < ec.n; i++) {
        printf("%s", ec.rcode[i]);
        if (i + 1 < ec.n) printf(",");
    }
    printf("}\n");

    AnnotRuns ar = build_annotated_runs(&ec);

    printf("E annotated runs = {");
    for (size_t i = 0; i < ar.n; i++) {
        printf("<|TerminalCode->%s,R1RunLength->%d|>", ar.a[i].terminal, ar.a[i].runlen);
        if (i + 1 < ar.n) printf(",");
    }
    printf("}\n");

    OpenESuffix openE = extract_open_e_suffix(states->a, states->n);
    if (openE.exists) {
        printf("Open E-suffix = ");
        print_state_word(openE.suffix, openE.suffix_len);
        printf("\n");

        printf("Open R1-run length = %d\n", openE.open_runlen);

        printf("Open remainder = ");
        print_state_word(openE.remainder, openE.remainder_len);
        printf("\n");
    } else {
        printf("Open E-suffix = {}\n");
        printf("Open R1-run length = 0\n");
        printf("Open remainder = {}\n");
    }

    size_t L = nontriv_j.n < ar.n ? nontriv_j.n : ar.n;
    printf("comparison = {");
    int overlap_ok = 1;
    for (size_t i = 0; i < L; i++) {
        int same = (nontriv_j.a[i] == ar.a[i].runlen);
        printf("{%d,%d,%s}", nontriv_j.a[i], ar.a[i].runlen, same ? "True" : "False");
        if (i + 1 < L) printf(",");
        if (!same) overlap_ok = 0;
    }
    printf("}\n");
    printf("ExactMatchOnOverlap = %s\n", overlap_ok ? "True" : "False");
    printf("SameLength = %s\n", (nontriv_j.n == ar.n) ? "True" : "False");
    printf("ExactMatch = %s\n", (overlap_ok && nontriv_j.n == ar.n) ? "True" : "False");

    if (csv && ar.n >= 2) {
        for (size_t i = 0; i + 1 < ar.n; i++) {
            write_transition_csv(csv, m, i,
                                 ar.a[i].runlen, ar.a[i].terminal,
                                 ar.a[i + 1].runlen, ar.a[i + 1].terminal);
        }
    }
    if (csv && openE.exists) {
        write_open_suffix_csv(csv, m, &openE);
        fflush(csv);
    }

    printf("========================================\n");

    free_open_e_suffix(&openE);
    free(nontriv_j.a);
    free_ecode(&ec);
    free_annotruns(&ar);
    wordlist_free(&dwords);
    wordlist_free(&ewords);
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    int max_m = DEFAULT_MAX_M;
    const char *backing_name = "Q_backing_win64.bin";
    const char *csv_name = "jt_transitions_win64_open.csv";

    if (argc >= 2) max_m = atoi(argv[1]);
    if (argc >= 3) backing_name = argv[2];
    if (argc >= 4) csv_name = argv[3];

    if (max_m < 3) {
        fprintf(stderr, "Need max_m >= 3\n");
        return 1;
    }

    uint64_t Nmax = (1ULL << max_m);
    uint64_t entries = Nmax + 1;

    fprintf(stderr, "Allocating exact Q array up to 2^%d = %" PRIu64 " entries\n", max_m, Nmax);
    fprintf(stderr, "Backing file: %s\n", backing_name);
    fprintf(stderr, "CSV file: %s\n", csv_name);
    fprintf(stderr, "Storage type: uint64_t\n");
    fprintf(stderr, "Approx mapped size: %.2f GiB\n",
            (double)entries * (double)sizeof(QTYPE) / (1024.0 * 1024.0 * 1024.0));

    FILE *csv = fopen(csv_name, "w");
    if (!csv) {
        perror("fopen csv");
        return 1;
    }
    write_csv_header(csv);

    FileMappedArray fm = fmap_create_u64(backing_name, entries);
    QTYPE *Q = fm.ptr;

    memset(Q, 0, (size_t)(entries * sizeof(QTYPE)));

    Q[0] = 0;
    Q[1] = 1;
    Q[2] = 1;

    VecI64 Ehist = {0}, Fhist = {0}, Ghist = {0}, Khist = {0}, Deltahist = {0};
    VecI cseq = {0}, dseq = {0};
    VecC states = {0};

    for (uint64_t n = 3; n <= Nmax; n++) {
        uint64_t aidx = n - Q[n - 1];
        uint64_t bidx = n - Q[n - 2];

        int64_t alt = (n & 1ULL) ? -1 : +1;
        Q[n] = Q[aidx] + Q[bidx] + alt;

        if (is_power_of_two_u64(n)) {
            int m = floor_log2_u64(n);

            int64_t half = (int64_t)(n >> 1);
            int64_t Em = (int64_t)Q[n - 1] - half;
            int64_t Fm = (int64_t)Q[n - 2] - half;
            int64_t Gm = (int64_t)Q[n - 3] - half;
            int64_t kappa = Em + Fm;
            int64_t delta = Em - Gm;

            vec_i64_push(&Ehist, Em);
            vec_i64_push(&Fhist, Fm);
            vec_i64_push(&Ghist, Gm);
            vec_i64_push(&Khist, kappa);
            vec_i64_push(&Deltahist, delta);

            if (Khist.n >= 2) {
                int c = (int)((Khist.a[Khist.n - 1] - Khist.a[Khist.n - 2]) / 2);
                vec_i_push(&cseq, c);
            }
            if (Deltahist.n >= 1) {
                int d = (int)(Deltahist.a[Deltahist.n - 1] / 2);
                vec_i_push(&dseq, d);
            }

            if (cseq.n >= 4) {
                int b4 =
                    (cseq.a[cseq.n - 4] << 3) |
                    (cseq.a[cseq.n - 3] << 2) |
                    (cseq.a[cseq.n - 2] << 1) |
                    (cseq.a[cseq.n - 1] << 0);
                char s = state_from_block4(b4);
                vec_c_push(&states, s);
            }

            analyze_level(m,
                          &Ehist, &Fhist, &Ghist, &Khist, &Deltahist,
                          &cseq, &dseq, &states,
                          csv);
        }

        if ((n % 100000000ULL) == 0ULL) {
            fprintf(stderr, "n=%" PRIu64 "\n", n);
        }
    }

    fflush(csv);
    fclose(csv);

    fmap_close(&fm);

    free(Ehist.a);
    free(Fhist.a);
    free(Ghist.a);
    free(Khist.a);
    free(Deltahist.a);
    free(cseq.a);
    free(dseq.a);
    free(states.a);

    return 0;
}
