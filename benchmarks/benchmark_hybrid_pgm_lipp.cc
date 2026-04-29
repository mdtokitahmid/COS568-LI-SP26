#include "benchmarks/benchmark_hybrid_pgm_lipp.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

/**
 * Sweep over (pgm_error, flush_threshold_pct) combinations.
 *
 * pgm_error         : epsilon for the internal DPGM write buffer.
 *                     Smaller  → more segments, slower merges, faster lookups.
 *                     Larger   → fewer segments, faster merges, slower lookups.
 *
 * flush_threshold_pct: flush DPGM → LIPP when DPGM holds this % of the
 *                      initial bulk-loaded key count.
 *                      Smaller  → more frequent flushes, DPGM stays tiny,
 *                                 lookups fast but insert throughput hurts.
 *                      Larger   → fewer flushes, DPGM grows, lookup cost rises.
 */
void benchmark_64_hybrid(tli::Benchmark<uint64_t>& benchmark) {
    // --- flush_threshold_pct = 1% ---
    benchmark.template Run<HybridPGMLIPP<uint64_t,  64, 1>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 128, 1>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 256, 1>>();

    // --- flush_threshold_pct = 5% ---
    benchmark.template Run<HybridPGMLIPP<uint64_t,  64, 5>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 128, 5>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 256, 5>>();

    // --- flush_threshold_pct = 10% ---
    benchmark.template Run<HybridPGMLIPP<uint64_t,  64, 10>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 128, 10>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 256, 10>>();

    // --- flush_threshold_pct = 20% ---
    benchmark.template Run<HybridPGMLIPP<uint64_t,  64, 20>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 128, 20>>();
    benchmark.template Run<HybridPGMLIPP<uint64_t, 256, 20>>();
}
