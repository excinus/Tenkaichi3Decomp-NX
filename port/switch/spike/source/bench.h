#ifndef SPIKE_BENCH_H
#define SPIKE_BENCH_H
#include <stdint.h>

#define BENCH_SF_ITERS 4000000u
#define BENCH_MEM_ITERS 20000000u

typedef struct BenchResult {
    uint64_t ns, ops;
    uint32_t sink; /* a value from the results, printed so that the work is not left out by the compiler */
} BenchResult;

uint64_t Bench_NowNs(void); /* provided by the program (main.c on the Switch) */
void Bench_SoftFloat(BenchResult *out, uint32_t iters);
void Bench_Memory(BenchResult *out, uint32_t iters);

#endif
