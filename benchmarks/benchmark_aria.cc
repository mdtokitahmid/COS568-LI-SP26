#include "benchmarks/benchmark_aria.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/aria.h"

/**
 * Sweep over (pgm_error, flush_interval) combinations for the async hybrid.
 *
 * pgm_error      : epsilon for the DPGM write buffer.
 * flush_interval : flush dpgm_active_ to LIPP after this many inserts.
 *
 * Note: There is no compile-time workload switch.  The same index implementation
 * runs on both mixed workloads and uses runtime one-shot profiling to choose
 * between a read-optimized write-through mode and a DPGM write-buffered mode.
 */
void benchmark_64_hybrid_async(tli::Benchmark<uint64_t>& benchmark) {
    
    // We register the dynamically adapting index with different DPGM epsilons.
    // The Python script will test all three of these variants on BOTH workloads.
    // 
    // For the 10% insert workload: the index should detect read-heavy behavior,
    // flush any early DPGM-buffered inserts, then use the read-optimized mode.
    //
    // For the 90% insert workload: The index will detect the writes, lock into
    // For the 90% insert workload: the index should stay in DPGM write-buffered
    // mode.  The analysis picks the best epsilon for that workload.
    
    benchmark.template Run<ARIA<uint64_t, 64,  2000000>>();
    benchmark.template Run<ARIA<uint64_t, 128, 2000000>>();
    benchmark.template Run<ARIA<uint64_t, 256, 2000000>>();
}
