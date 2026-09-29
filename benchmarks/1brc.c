/* 1brc.c — the 1BRC challenge in plain C for speed comparison.
 * Same task as benchmarks/1brc.hphp, compiled with the same gcc -O2 that
 * hphp uses as its codegen backend.
 * Build: gcc -O2 -std=c11 benchmarks/1brc.c -o benchmarks/1brc_c.exe
 * Run:   benchmarks/1brc_c.exe measurements.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTA 65536

typedef struct {
    const char *name;   /* pointer into the mmap'd block; not NUL-terminated */
    int len;
    long long count;
    int mn, mx;
    long long sum;      /* fixed point, 1 decimal */
} Sta;

static Sta table[NSTA];

static int name_cmp(const void *pa, const void *pb) {
    const Sta *a = *(const Sta *const *)pa;
    const Sta *b = *(const Sta *const *)pb;
    int min = a->len < b->len ? a->len : b->len;
    int c = memcmp(a->name, b->name, (size_t)min);
    if (c != 0) return c;
    return a->len - b->len;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1]
        : "C:/Users/elias/Downloads/1brc-main/1brc-main/measurements.txt";
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }

    /* read the whole file in one gulp */
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)fsz);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }
    size_t got = fread(buf, 1, (size_t)fsz, f);
    fclose(f);

    char *p = buf, *end = buf + got;
    while (p < end) {
        char *semi = memchr(p, ';', (size_t)(end - p));
        if (!semi) break;
        char *nl = memchr(semi, '\n', (size_t)(end - semi));
        if (!nl) nl = end;
        const char *name = p;
        int nlen = (int)(semi - p);
        /* fixed-point parse of -999..999 with one decimal */
        const char *q = semi + 1;
        int neg = 0, ip = 0, fp = 0;
        if (*q == '-') { neg = 1; q++; }
        while (q < nl && *q >= '0' && *q <= '9') ip = ip * 10 + (*q++ - '0');
        if (q < nl && *q == '.') { q++; fp = *q - '0'; }
        int t = ip * 10 + fp;
        if (neg) t = -t;

        /* FNV-1a over the station name, open addressing */
        unsigned h = 2166136261u;
        for (int i = 0; i < nlen; i++) h = (h ^ (unsigned char)name[i]) * 16777619u;
        unsigned idx = h & (NSTA - 1);
        while (table[idx].name) {
            if (table[idx].len == nlen && memcmp(table[idx].name, name, (size_t)nlen) == 0)
                break;
            idx = (idx + 1) & (NSTA - 1);
        }
        if (!table[idx].name) {
            table[idx].name = name;   /* stable: buf outlives the table */
            table[idx].len = nlen;
            table[idx].count = 0;
            table[idx].mn = 10000; table[idx].mx = -10000;
            table[idx].sum = 0;
        }
        Sta *s = &table[idx];
        s->count++;
        if (t < s->mn) s->mn = t;
        if (t > s->mx) s->mx = t;
        s->sum += t;

        p = nl + 1;
    }

    Sta *order[NSTA];
    int n = 0;
    for (int i = 0; i < NSTA; i++)
        if (table[i].name) order[n++] = &table[i];
    qsort(order, (size_t)n, sizeof(Sta *), name_cmp);

    putchar('{');
    for (int i = 0; i < n; i++) {
        Sta *s = order[i];
        if (i) fputs(", ", stdout);
        /* mean: exact integer tenths, rounded half-up (away from zero) —
         * the official 1BRC rule, identical to the other implementations */
        long long cnt = s->count, total = s->sum;
        int m10 = total >= 0 ? (int)((2 * total + cnt) / (2 * cnt))
                             : -(int)((2 * -total + cnt) / (2 * cnt));
        double mean = m10 / 10.0;
        double mn = s->mn / 10.0, mx = s->mx / 10.0;
        if (mn == 0.0) mn = 0.0;    /* normalize -0.0 */
        if (mx == 0.0) mx = 0.0;
        if (mean == 0.0) mean = 0.0;
        printf("%.*s=%.1f/%.1f/%.1f", s->len, s->name, mn, mean, mx);
    }
    puts("}");
    return 0;
}
