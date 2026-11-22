#define _GNU_SOURCE
#include "thread_affinity.h"
#include <stdio.h>

static int exactly_cpu(int cpu) {
    cpu_set_t got;
    if (sched_getaffinity(0, sizeof got, &got) != 0) return 0;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &got) != (i == cpu)) return 0;
    return 1;
}

int main(void) {
    cpu_set_t original;
    cpu_set_t main_mask, worker_mask;
    if (sched_getaffinity(0, sizeof original, &original) != 0) return 2;
    if (setenv("SC_AFFINITY", "0:1", 1) != 0) return 2;
    if (sc_affinity_mask(getenv("SC_AFFINITY"), 0, &main_mask) != 1 ||
        sc_affinity_mask(getenv("SC_AFFINITY"), 1, &worker_mask) != 1 ||
        CPU_COUNT(&main_mask) != 1 || !CPU_ISSET(0, &main_mask) ||
        CPU_COUNT(&worker_mask) != 1 || !CPU_ISSET(1, &worker_mask)) {
        fprintf(stderr, "affinity_test: parser did not produce masks 0:1\n"); return 1;
    }
    if (!CPU_ISSET(0, &original) || !CPU_ISSET(1, &original)) {
        puts("x86_affinity_test: parser PASS; sched_setaffinity SKIP (CPU 0/1 outside allowed cpuset)");
        return 0;
    }
    if (sc_affinity_apply(0) != 0 || !exactly_cpu(0)) {
        (void)sched_setaffinity(0, sizeof original, &original);
        fprintf(stderr, "affinity_test: main mask 0 failed\n"); return 1;
    }
    if (sc_affinity_apply(1) != 0 || !exactly_cpu(1)) {
        (void)sched_setaffinity(0, sizeof original, &original);
        fprintf(stderr, "affinity_test: worker mask 1 failed\n"); return 1;
    }
    if (sched_setaffinity(0, sizeof original, &original) != 0) return 2;
    puts("x86_affinity_test: PASS (SC_AFFINITY=0:1, sched_getaffinity verified)");
    return 0;
}
