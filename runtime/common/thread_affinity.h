/* Optional Linux big.LITTLE CPU pinning. Include only in Linux runtime TUs. */
#ifndef SC_THREAD_AFFINITY_H
#define SC_THREAD_AFFINITY_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Parse comma-separated CPU ids/ranges into a nonempty cpu_set_t. */
static inline int sc_affinity_parse_list(const char *s, cpu_set_t *set) {
    unsigned cpu, end;
    int digits;
    CPU_ZERO(set);
    if (!s || !*s) return -1;
    while (*s) {
        cpu = 0; digits = 0;
        while (*s >= '0' && *s <= '9') { cpu = cpu * 10u + (unsigned)(*s++ - '0'); digits = 1; if (cpu >= CPU_SETSIZE) return -1; }
        if (!digits) return -1;
        end = cpu;
        if (*s == '-') {
            ++s; end = 0; digits = 0;
            while (*s >= '0' && *s <= '9') { end = end * 10u + (unsigned)(*s++ - '0'); digits = 1; if (end >= CPU_SETSIZE) return -1; }
            if (!digits || end < cpu) return -1;
        }
        for (; cpu <= end; ++cpu) CPU_SET(cpu, set);
        if (!*s) break;
        if (*s++ != ',' || !*s) return -1;
    }
    return CPU_COUNT(set) ? 0 : -1;
}

/* Returns 1 for a parsed mask, 0 when unset/auto is not applicable, -1 for malformed input. */
static inline int sc_affinity_mask(const char *spec, int worker, cpu_set_t *set) {
    const char *sep;
    cpu_set_t main_set, worker_set;
    long ncpu;
    if (!spec || !*spec) return 0;
    if (!strcmp(spec, "rk3399") || !strcmp(spec, "auto")) {
        ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpu < 6) return 0;
        CPU_ZERO(&main_set); CPU_ZERO(&worker_set);
        CPU_SET(4, &main_set); CPU_SET(5, &main_set);
        for (int i = 0; i <= 3; ++i) CPU_SET(i, &worker_set);
        *set = worker ? worker_set : main_set;
        return 1;
    }
    sep = spec;
    while (*sep && *sep != ':') ++sep;
    if (*sep != ':') return -1;
    {
        size_t main_len = (size_t)(sep - spec);
        char main_text[256];
        if (!main_len || main_len >= sizeof main_text || !sep[1]) return -1;
        for (size_t i = 0; i < main_len; ++i) main_text[i] = spec[i];
        main_text[main_len] = 0;
        if (sc_affinity_parse_list(main_text, &main_set) || sc_affinity_parse_list(sep + 1, &worker_set)) return -1;
    }
    *set = worker ? worker_set : main_set;
    return 1;
}

static inline int sc_affinity_apply(int worker) {
    const char *spec = getenv("SC_AFFINITY");
    cpu_set_t set;
    int rc = sc_affinity_mask(spec, worker, &set);
    if (rc <= 0) return rc;
    return sched_setaffinity(0, sizeof set, &set);
}
#endif
