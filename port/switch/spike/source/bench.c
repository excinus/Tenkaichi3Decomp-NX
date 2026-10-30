/*
 * The processor test of the Switch spike, also built for a PC so that the two can be compared.
 *
 * The game does every float operation through the port's software float (port/src/softfloat_ps2.c: the PS2's
 * arithmetic, the same on every host); that is most of its processor time. This runs the same routines in a
 * dependent chain (each result feeds the next call, as in the game's vector maths) and a memory walk over 16 MB
 * (the game's heap is 28 MB), and reports nanoseconds per operation.
 *
 *   Switch:  part of spike.nro (port/switch/spike/build.sh)
 *   PC:      cc -O2 -DBENCH_HOST -Iinclude -ffunction-sections -Wl,--gc-sections \
 *               port/switch/spike/source/bench.c port/src/softfloat_ps2.c -o bench && ./bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "bench.h"

/* The routines of port/src/softfloat_ps2.c: floats as 32-bit patterns. */
uint32_t __addsf3(uint32_t a, uint32_t b);
uint32_t __mulsf3(uint32_t a, uint32_t b);
uint32_t __divsf3(uint32_t a, uint32_t b);

#define ONE 0x3F800000u     /* 1.0f */
#define K_MUL 0x3F7FBE77u   /* 0.999f */
#define K_ADD 0x3C23D70Au   /* 0.01f */
#define K_DIV 0x3F800347u   /* 1.0001f */

void Bench_SoftFloat(BenchResult *out, uint32_t iters) {
    uint32_t x = ONE, y = ONE, i;
    uint64_t t0 = Bench_NowNs();

    for (i = 0; i < iters; i++) {
        x = __mulsf3(x, K_MUL);
        x = __addsf3(x, K_ADD);
        y = __addsf3(__mulsf3(y, x), K_ADD);
        if ((i & 7) == 0) {
            y = __divsf3(y, K_DIV);
        }
    }
    out->ns = Bench_NowNs() - t0;
    out->ops = (uint64_t)iters * 4 + (iters + 7) / 8;
    out->sink = x ^ y;
}

void Bench_Memory(BenchResult *out, uint32_t iters) {
    const uint32_t words = 16u << 18; /* 16 MB of 4-byte words */
    uint32_t *table = malloc((size_t)words * 4), i, at = 1, sum = 0;
    uint64_t t0;

    if (table == NULL) {
        out->ns = out->ops = 0;
        out->sink = 0;
        return;
    }
    for (i = 0; i < words; i++) {
        table[i] = i * 2654435761u;
    }
    t0 = Bench_NowNs();
    for (i = 0; i < iters; i++) { /* a random walk: each address depends on the last word read */
        at = (at * 1103515245u + 12345u + table[at & (words - 1)]) & (words - 1);
        sum += table[at];
        table[(at * 7) & (words - 1)] = sum;
    }
    out->ns = Bench_NowNs() - t0;
    out->ops = iters;
    out->sink = sum;
    free(table);
}

#ifdef BENCH_HOST
#include <time.h>

uint64_t Bench_NowNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

int main(void) {
    BenchResult r;

    Bench_SoftFloat(&r, BENCH_SF_ITERS);
    printf("soft float: %.2f ns per operation (%llu operations, check %08X)\n", (double)r.ns / (double)r.ops,
           (unsigned long long)r.ops, r.sink);
    Bench_Memory(&r, BENCH_MEM_ITERS);
    printf("memory walk: %.2f ns per step (%llu steps, check %08X)\n", (double)r.ns / (double)r.ops,
           (unsigned long long)r.ops, r.sink);
    return 0;
}
#endif
