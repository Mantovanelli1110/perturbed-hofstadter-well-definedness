#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROFILES 1024
#define MAX_LINE 4096
#define MAX_STATES 16

typedef struct {
    int profile_id;
    int len;
    char word[256];
    int weight;
    int minpref;
    char first;
    char last;
    int sid;   /* compressed state 0..8 */
} RawProfile;

static RawProfile PROFILES[MAX_PROFILES];
static int PROFILE_COUNT = 0;

static int SEEN[MAX_STATES][3][MAX_STATES][3];

/* ------------------------------------------------------------ */

static int signature_to_sid(int weight, int minpref, char first, char last) {
    if (weight == -2 && minpref == -2 && first == '-' && last == '-') return 0; /* S0 */
    if (weight ==  0 && minpref == -2 && first == '-' && last == '+') return 1; /* S1 */
    if (weight ==  0 && minpref == -1 && first == '+' && last == '+') return 2; /* S2 */
    if (weight ==  0 && minpref == -1 && first == '+' && last == '-') return 3; /* S3 */
    if (weight ==  0 && minpref == -1 && first == '-' && last == '+') return 4; /* S4 */
    if (weight ==  0 && minpref == -1 && first == '-' && last == '-') return 5; /* S5 */
    if (weight ==  0 && minpref ==  0 && first == '+' && last == '-') return 6; /* S6 */
    if (weight ==  2 && minpref ==  0 && first == '+' && last == '+') return 7; /* S7 */
    if (weight ==  2 && minpref ==  0 && first == '+' && last == '-') return 8; /* S8 */
    return -1;
}

static int load_profiles_csv(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 0;
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty profile file\n");
        fclose(fp);
        return 0;
    }

    PROFILE_COUNT = 0;

    while (fgets(line, sizeof(line), fp)) {
        RawProfile rp;
        memset(&rp, 0, sizeof(rp));
        char first[8], last[8];

        if (sscanf(line, "%d,%d,%255[^,],%d,%d,%7[^,],%7s",
                   &rp.profile_id, &rp.len, rp.word,
                   &rp.weight, &rp.minpref, first, last) != 7) {
            continue;
        }

        rp.first = first[0];
        rp.last = last[0];
        rp.sid = signature_to_sid(rp.weight, rp.minpref, rp.first, rp.last);

        if (rp.sid < 0) {
            fprintf(stderr,
                    "Unknown profile signature for profile %d: (%d,%d,%c,%c)\n",
                    rp.profile_id, rp.weight, rp.minpref, rp.first, rp.last);
            fclose(fp);
            return 0;
        }

        if (PROFILE_COUNT >= MAX_PROFILES) {
            fprintf(stderr, "Too many profiles\n");
            fclose(fp);
            return 0;
        }

        PROFILES[PROFILE_COUNT++] = rp;
    }

    fclose(fp);
    return 1;
}

static int profile_id_to_sid(int pid) {
    for (int i = 0; i < PROFILE_COUNT; i++) {
        if (PROFILES[i].profile_id == pid) return PROFILES[i].sid;
    }
    return -1;
}

static void compress_edges(const char *edges_csv) {
    FILE *fp = fopen(edges_csv, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", edges_csv);
        exit(1);
    }

    memset(SEEN, 0, sizeof(SEEN));

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty edge file\n");
        fclose(fp);
        exit(1);
    }

    int compressed_count = 0;

    printf("static const int REAL_EDGES[][4] = {\n");

    while (fgets(line, sizeof(line), fp)) {
        int fp_id, fd, tp_id, td;
        unsigned long long cnt;

        if (sscanf(line, "%d,%d,%d,%d,%llu",
                   &fp_id, &fd, &tp_id, &td, &cnt) != 5) {
            continue;
        }

        int fsid = profile_id_to_sid(fp_id);
        int tsid = profile_id_to_sid(tp_id);

        if (fsid < 0 || tsid < 0) {
            fprintf(stderr, "Missing profile id in map: %d or %d\n", fp_id, tp_id);
            fclose(fp);
            exit(1);
        }

        if (!(fd == 0 || fd == 2 || td == 0 || td == 2)) {
            /* keep indexing simple: debt values are expected to be 0 or 2 */
        }

        if (!SEEN[fsid][fd][tsid][td]) {
            SEEN[fsid][fd][tsid][td] = 1;
            printf("    {%d,%d,%d,%d},\n", fsid, fd, tsid, td);
            compressed_count++;
        }
    }

    printf("};\n");
    printf("\n/* compressed edge count = %d */\n", compressed_count);

    fclose(fp);
}

int main(int argc, char **argv) {
    const char *profiles_csv = "D_macro_profiles_34.csv";
    const char *edges_csv = "D_debt_edges_34.csv";

    if (argc >= 2) profiles_csv = argv[1];
    if (argc >= 3) edges_csv = argv[2];

    if (!load_profiles_csv(profiles_csv)) {
        return 1;
    }

    printf("Loaded raw profiles: %d\n", PROFILE_COUNT);
    compress_edges(edges_csv);

    return 0;
}