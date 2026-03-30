#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <windows.h>

#ifndef DEFAULT_MAX_M
#define DEFAULT_MAX_M 34
#endif

#ifndef DEFAULT_BLOCKLEN
#define DEFAULT_BLOCKLEN 16
#endif

typedef uint64_t QTYPE;

/* ---------- utility ---------- */

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
    if (fm.hFile == INVALID_HANDLE_VALUE) die_last_error("CreateFileA failed");

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)bytes;
    if (!SetFilePointerEx(fm.hFile, li, NULL, FILE_BEGIN)) die_last_error("SetFilePointerEx failed");
    if (!SetEndOfFile(fm.hFile)) die_last_error("SetEndOfFile failed");

    fm.hMap = CreateFileMappingA(
        fm.hFile, NULL, PAGE_READWRITE, sizeHigh, sizeLow, NULL
    );
    if (!fm.hMap) die_last_error("CreateFileMappingA failed");

    fm.ptr = (QTYPE *)MapViewOfFile(fm.hMap, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)bytes);
    if (!fm.ptr) die_last_error("MapViewOfFile failed");

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

/* ---------- profiles ---------- */

typedef struct {
    char *word;
    int len;
    int weight;
    int minpref;
    char first;
    char last;
} Profile;

typedef struct {
    Profile *a;
    size_t n, cap;
} ProfileTable;

static void profile_table_init(ProfileTable *pt) {
    pt->a = NULL;
    pt->n = pt->cap = 0;
}

static void profile_table_free(ProfileTable *pt) {
    if (!pt) return;
    for (size_t i = 0; i < pt->n; i++) free(pt->a[i].word);
    free(pt->a);
    pt->a = NULL;
    pt->n = pt->cap = 0;
}

static int profile_equal(const Profile *p, const char *word, int len,
                         int weight, int minpref, char first, char last) {
    return p->len == len &&
           p->weight == weight &&
           p->minpref == minpref &&
           p->first == first &&
           p->last == last &&
           memcmp(p->word, word, (size_t)len) == 0;
}

static int profile_table_intern(ProfileTable *pt, const char *word, int len,
                                int weight, int minpref, char first, char last) {
    for (size_t i = 0; i < pt->n; i++) {
        if (profile_equal(&pt->a[i], word, len, weight, minpref, first, last)) {
            return (int)i;
        }
    }

    if (pt->n == pt->cap) {
        pt->cap = pt->cap ? 2 * pt->cap : 64;
        pt->a = (Profile *)realloc(pt->a, pt->cap * sizeof(Profile));
        if (!pt->a) {
            fprintf(stderr, "realloc profile table failed\n");
            exit(1);
        }
    }

    Profile *p = &pt->a[pt->n];
    p->word = (char *)malloc((size_t)len + 1);
    if (!p->word) {
        fprintf(stderr, "malloc profile word failed\n");
        exit(1);
    }
    memcpy(p->word, word, (size_t)len);
    p->word[len] = '\0';
    p->len = len;
    p->weight = weight;
    p->minpref = minpref;
    p->first = first;
    p->last = last;

    pt->n++;
    return (int)(pt->n - 1);
}

static int compute_profile_id(ProfileTable *pt, const int *dvals, int len) {
    int sum = 0;
    int minpref = 0;
    char *word = (char *)malloc((size_t)len);
    if (!word) {
        fprintf(stderr, "malloc word failed\n");
        exit(1);
    }

    for (int i = 0; i < len; i++) {
        if (dvals[i] == +1) word[i] = '+';
        else if (dvals[i] == -1) word[i] = '-';
        else word[i] = '?';

        sum += dvals[i];
        if (sum < minpref) minpref = sum;
    }

    char first = word[0];
    char last = word[len - 1];
    int id = profile_table_intern(pt, word, len, sum, minpref, first, last);
    free(word);
    return id;
}

/* ---------- edge table ---------- */

typedef struct {
    int prof_from;
    int debt_from;
    int prof_to;
    int debt_to;
    uint64_t count;
} Edge;

typedef struct {
    Edge *a;
    size_t n, cap;
} EdgeTable;

static void edge_table_init(EdgeTable *et) {
    et->a = NULL;
    et->n = et->cap = 0;
}

static void edge_table_free(EdgeTable *et) {
    free(et->a);
    et->a = NULL;
    et->n = et->cap = 0;
}

static void edge_table_add(EdgeTable *et, int pf, int df, int pt, int dt) {
    for (size_t i = 0; i < et->n; i++) {
        if (et->a[i].prof_from == pf &&
            et->a[i].debt_from == df &&
            et->a[i].prof_to == pt &&
            et->a[i].debt_to == dt) {
            et->a[i].count++;
            return;
        }
    }

    if (et->n == et->cap) {
        et->cap = et->cap ? 2 * et->cap : 128;
        et->a = (Edge *)realloc(et->a, et->cap * sizeof(Edge));
        if (!et->a) {
            fprintf(stderr, "realloc edge table failed\n");
            exit(1);
        }
    }

    et->a[et->n].prof_from = pf;
    et->a[et->n].debt_from = df;
    et->a[et->n].prof_to = pt;
    et->a[et->n].debt_to = dt;
    et->a[et->n].count = 1;
    et->n++;
}

static int debt_after_profile(int debt, const Profile *p, int *ok) {
    if (debt - p->minpref > 2) {
        *ok = 0;
        return 0;
    }

    int final_sum = -debt + p->weight;
    int next_debt = (final_sum < 0) ? -final_sum : 0;
    if (next_debt > 2) {
        *ok = 0;
        return 0;
    }

    *ok = 1;
    return next_debt;
}

/* ---------- CSV output ---------- */

static void write_profiles_csv(const char *filename, const ProfileTable *pt) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen profiles");
        exit(1);
    }
    fprintf(fp, "profile_id,len,word,weight,minpref,first,last\n");
    for (size_t i = 0; i < pt->n; i++) {
        fprintf(fp, "%zu,%d,%s,%d,%d,%c,%c\n",
                i,
                pt->a[i].len,
                pt->a[i].word,
                pt->a[i].weight,
                pt->a[i].minpref,
                pt->a[i].first,
                pt->a[i].last);
    }
    fclose(fp);
}

static void write_edges_csv(const char *filename, const EdgeTable *et) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen edges");
        exit(1);
    }
    fprintf(fp, "from_profile,from_debt,to_profile,to_debt,count\n");
    for (size_t i = 0; i < et->n; i++) {
        fprintf(fp, "%d,%d,%d,%d,%" PRIu64 "\n",
                et->a[i].prof_from,
                et->a[i].debt_from,
                et->a[i].prof_to,
                et->a[i].debt_to,
                et->a[i].count);
    }
    fclose(fp);
}

/* ---------- occurrence CSV ---------- */

static FILE *open_occ_csv(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen macro occurrences");
        exit(1);
    }
    fprintf(fp, "macro_index,profile_id,debt_before\n");
    return fp;
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    int max_m = DEFAULT_MAX_M;
    int BL = DEFAULT_BLOCKLEN;
    const char *backing_name = "Q_backing_profiles_stream.bin";
    const char *profiles_csv = "D_macro_profiles_stream.csv";
    const char *edges_csv = "D_debt_edges_stream.csv";
    const char *occ_csv = "macro_occurrences.csv";

    if (argc >= 2) max_m = atoi(argv[1]);
    if (argc >= 3) BL = atoi(argv[2]);
    if (argc >= 4) backing_name = argv[3];
    if (argc >= 5) profiles_csv = argv[4];
    if (argc >= 6) edges_csv = argv[5];
    if (argc >= 7) occ_csv = argv[6];

    if (max_m < 4) {
        fprintf(stderr, "Need max_m >= 4\n");
        return 1;
    }
    if (BL <= 0) {
        fprintf(stderr, "Need block length > 0\n");
        return 1;
    }

    uint64_t Nmax = (1ULL << max_m);
    uint64_t entries = Nmax + 1ULL;

    fprintf(stderr, "Allocating exact Q array up to 2^%d = %" PRIu64 " entries\n", max_m, Nmax);
    fprintf(stderr, "Macro-block length = %d\n", BL);
    fprintf(stderr, "Approx mapped size: %.2f GiB\n",
            (double)entries * (double)sizeof(QTYPE) / (1024.0 * 1024.0 * 1024.0));

    FileMappedArray fm = fmap_create_u64(backing_name, entries);
    QTYPE *Q = fm.ptr;
    memset(Q, 0, (size_t)(entries * sizeof(QTYPE)));

    Q[0] = 0;
    Q[1] = 1;
    Q[2] = 1;

    ProfileTable PT;
    profile_table_init(&PT);

    EdgeTable ET;
    edge_table_init(&ET);

    FILE *occ_fp = open_occ_csv(occ_csv);

    int *macro_buf = (int *)malloc((size_t)BL * sizeof(int));
    if (!macro_buf) {
        fprintf(stderr, "malloc macro_buf failed\n");
        exit(1);
    }

    int current_m = -1;
    int macro_fill = 0;
    int prev_prof = -1;
    int debt = 0;
    uint64_t macro_index = 0;

    for (uint64_t n = 3; n <= Nmax; n++) {
        uint64_t u = n - Q[n - 1];
        uint64_t v = n - Q[n - 2];
        int64_t alt = (n & 1ULL) ? -1 : +1;
        Q[n] = Q[u] + Q[v] + alt;

        int m = floor_log2_u64(n);
        if (m < 1) continue;

        if (current_m == -1) current_m = m;

        if (m != current_m) {
            /* discard incomplete trailing macro-block at dyadic boundary */
            macro_fill = 0;
            prev_prof = -1;
            debt = 0;
            macro_index = 0;
            current_m = m;
        }

        int64_t D = (int64_t)Q[n] - (int64_t)Q[n - 2] - 1;
        macro_buf[macro_fill++] = (int)D;

        if (macro_fill == BL) {
            int pid = compute_profile_id(&PT, macro_buf, BL);

            /* log exact occurrence before debt update */
            fprintf(occ_fp, "%" PRIu64 ",%d,%d\n", macro_index, pid, debt);

            int ok = 0;
            int next_debt = debt_after_profile(debt, &PT.a[pid], &ok);

            if (prev_prof != -1 && ok) {
                edge_table_add(&ET, prev_prof, debt, pid, next_debt);
            }

            prev_prof = pid;
            if (ok) debt = next_debt;

            macro_fill = 0;
            macro_index++;
        }

        if ((n % 100000000ULL) == 0ULL) {
            fprintf(stderr, "n=%" PRIu64 "\n", n);
        }
    }

    fclose(occ_fp);

    write_profiles_csv(profiles_csv, &PT);
    write_edges_csv(edges_csv, &ET);

    printf("Distinct profiles: %zu\n", PT.n);
    printf("Distinct realized debt-transitions: %zu\n", ET.n);
    printf("Wrote %s, %s, and %s\n", profiles_csv, edges_csv, occ_csv);

    free(macro_buf);
    profile_table_free(&PT);
    edge_table_free(&ET);
    fmap_close(&fm);
    return 0;
}
