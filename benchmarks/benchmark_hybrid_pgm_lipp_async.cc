#include "benchmarks/benchmark_hybrid_pgm_lipp_async.h"

#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp_async.h"

/**
 * Sweep over (pgm_error, flush_interval) combinations for the async hybrid.
 *
 * pgm_error     : epsilon for the DPGM write buffer.
 *                 Smaller → more model segments, faster lookups in DPGM.
 *                 Larger  → fewer segments, cheaper inserts into DPGM.
 *
 * flush_interval: flush dpgm_active_ to LIPP after this many inserts.
 *                 Absolute count (not %) — ensures flush fires within the
 *                 2M-op benchmark window regardless of initial dataset size.
 *
 * Benchmark has 2M ops total:
 *   10% insert workload →  200K inserts: intervals 10K(×20), 50K(×4), 100K(×2)
 *   90% insert workload → 1.8M inserts: intervals 50K(×36), 100K(×18), 500K(×3)
 */
void benchmark_64_hybrid_async(tli::Benchmark<uint64_t>& benchmark) {
    // --- flush_interval = 10,000 inserts (very frequent, DPGM tiny) ---
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t,  64, 10000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 128, 10000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 256, 10000>>();

    // --- flush_interval = 50,000 inserts ---
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t,  64, 50000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 128, 50000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 256, 50000>>();

    // --- flush_interval = 100,000 inserts ---
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t,  64, 100000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 128, 100000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 256, 100000>>();

    // --- flush_interval = 500,000 inserts ---
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t,  64, 500000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 128, 500000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 256, 500000>>();

    // --- flush_interval = 2,000,000 (deferred: never flushes within 2M-op run) ---
    // Models a "write-optimized" configuration: DPGM absorbs all inserts at
    // full DPGM speed; the flush to LIPP is deferred to after the benchmark.
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t,  64, 2000000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 128, 2000000>>();
    benchmark.template Run<HybridPGMLIPPAsync<uint64_t, 256, 2000000>>();
}
