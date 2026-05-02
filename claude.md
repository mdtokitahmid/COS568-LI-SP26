# COS568 Learned Index Project Progress

This repository is at the Milestone 3 stage for the DynamicPGM + LIPP hybrid learned index project. The current implementation includes both the Milestone 2 synchronous hybrid and a Milestone 3 async double-DPGM hybrid.

## Project Goal

The project compares DynamicPGM, LIPP, and a hybrid learned index on 100M-key datasets. The workload generator withholds future insert keys from the initial bulkload:

- 90% insert workload: 2M total operations, 1.8M inserts, 200K lookups, initial bulkload is about 98.2M keys.
- 10% insert workload: 2M total operations, 200K inserts, 1.8M lookups, initial bulkload is about 99.8M keys.

The intended hybrid strategy is:

- Bulk-load the initial keys into LIPP.
- Send new inserts into a DynamicPGM write buffer.
- Optionally flush/migrate the buffer into LIPP.
- Use LIPP for the large old-key region and DynamicPGM for recent inserts.

The observed workload story is expected to be asymmetric. The hybrid can beat DynamicPGM in insert-heavy workloads because inserts remain cheap in a small DPGM buffer while many old-key lookups hit fast in LIPP. Beating pure LIPP in lookup-heavy workloads is much harder because most lookups already hit LIPP quickly, while hybrid misses and recently inserted positives require checking both LIPP and DPGM.

## Implemented Indexes

### Baselines

- `DynamicPGM`: implemented in `competitors/dynamic_pgm_index.h`, benchmarked through `benchmarks/benchmark_dynamic_pgm.cc`.
- `LIPP`: wrapper in `competitors/lipp.h`, benchmarked through `benchmarks/benchmark_lipp.cc`.
- `BTree` and static `PGM` are still wired into the benchmark binary, though Milestone 3 focuses on DynamicPGM, LIPP, and hybrids.

### Milestone 2 Hybrid

Files:

- `competitors/hybrid_pgm_lipp.h`
- `benchmarks/benchmark_hybrid_pgm_lipp.h`
- `benchmarks/benchmark_hybrid_pgm_lipp.cc`

Behavior:

- Bulk-loads all initial keys into LIPP.
- Inserts new keys into one DynamicPGM buffer.
- Flushes synchronously when `dpgm_count * 100 >= flush_threshold_pct * initial_size`.
- Flush inserts every DPGM key into LIPP one by one, then resets the DPGM.
- Lookup checks DPGM first, then LIPP.

Swept parameters:

- `pgm_error`: 64, 128, 256
- `flush_threshold_pct`: 1, 5, 10, 20

This version is simple and correct for the assignment setting, but flushes block the benchmark thread.

### Milestone 3 Async Hybrid

Files:

- `competitors/hybrid_pgm_lipp_async.h`
- `benchmarks/benchmark_hybrid_pgm_lipp_async.h`
- `benchmarks/benchmark_hybrid_pgm_lipp_async.cc`

Behavior:

- Uses three logical storage locations:
  - `lipp_`: initial bulk-loaded keys and previously flushed keys.
  - `dpgm_active_`: current write buffer for new inserts.
  - `dpgm_flushing_`: previous active buffer being drained into LIPP.
- Inserts always go into `dpgm_active_`.
- When `dpgm_active_` reaches `flush_interval`, it is moved into `dpgm_flushing_`, a fresh active DPGM is created, and a background thread drains `dpgm_flushing_` into LIPP.
- Lookup order is LIPP first, then flushing DPGM if a flush is active, then active DPGM.
- The deferred/no-flush setting is represented by `flush_interval = 2000000`, which does not flush during the 2M-op benchmark.

Swept parameters:

- `pgm_error`: 64, 128, 256
- `flush_interval`: 10K, 50K, 100K, 500K, 2M

Important caveat:

- The background flush uses one `std::shared_mutex` exclusive lock for the whole LIPP insertion drain. This keeps LIPP thread-safe and lets new inserts continue into `dpgm_active_`, but lookups can block during an active flush. The best insert-heavy result is expected to come from the large `flush_interval = 2000000` lazy/no-flush configuration.

## Build Wiring

`CMakeLists.txt` builds one `benchmark` executable and includes:

- `benchmarks/benchmark_dynamic_pgm.cc`
- `benchmarks/benchmark_pgm.cc`
- `benchmarks/benchmark_lipp.cc`
- `benchmarks/benchmark_btree.cc`
- `benchmarks/benchmark_hybrid_pgm_lipp.cc`
- `benchmarks/benchmark_hybrid_pgm_lipp_async.cc`

`benchmark.cc` supports:

- `--only DynamicPGM`
- `--only LIPP`
- `--only HybridPGMLIPP`
- `--only HybridPGMLIPPAsync`

The async hybrid is single-thread only by design because the LIPP wrapper is not treated as safe for general multithreaded benchmark use. Its `applicable()` rejects multithreaded runs.

## Running Methods

### Build

From the repository root:

```bash
mkdir -p build
cd build
cmake ..
make -j4 benchmark generate
cd ..
```

There is already a `build/benchmark` binary in this checkout.

### Generate/download data and workloads

The README's default pipeline is:

```bash
bash scripts/download_dataset.sh
bash scripts/create_minimal_cmake.sh
bash scripts/generate_workloads.sh
bash scripts/build_benchmark.sh
```

Milestone 3 expects these dataset/workload names under `data/`:

```text
fb_100M_public_uint64
books_100M_public_uint64
osmc_100M_public_uint64

*_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix
*_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix
```

Each mixed workload also needs a corresponding `_bulkload` file, which the benchmark loads automatically when inserts are present.

### Run all Milestone 3 benchmarks

From the repository root:

```bash
bash scripts/run_async.sh
```

This runs, for each dataset and both 10%/90% insert mixed workloads:

- `DynamicPGM`
- `LIPP`
- `HybridPGMLIPP`
- `HybridPGMLIPPAsync`

Each run uses:

```bash
build/benchmark DATA_FILE OPS_FILE --through --csv --only INDEX -r 3
```

Results are written to `results/*_results_table.csv`.

### Submit on Slurm

Use:

```bash
sbatch scripts/submit_async.sh
```

Before submitting, update the `cd` path inside `scripts/submit_async.sh` if the project is not located at:

```text
/scratch/gpfs/MONA/Toki/Academic/COS568/COS568-LI-SP26
```

The current Slurm request is:

- 1 node
- 1 task
- 2 CPUs per task
- 32GB memory
- 6 hour time limit
- `cpu` partition

For 100M-key LIPP-heavy runs, 32GB may be tight depending on the cluster and dataset. The known hybrid memory footprint can be around 12.5GB on FB for the lazy/no-flush configuration, but baseline and build overhead should be considered.

### Run one benchmark manually

Example for FB, 90% insert:

```bash
build/benchmark \
  data/fb_100M_public_uint64 \
  data/fb_100M_public_uint64_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix \
  --through --csv --only HybridPGMLIPPAsync -r 3
```

Example for FB, 10% insert:

```bash
build/benchmark \
  data/fb_100M_public_uint64 \
  data/fb_100M_public_uint64_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix \
  --through --csv --only HybridPGMLIPPAsync -r 3
```

### Generate plots

After results exist:

```bash
python scripts/analysis_m3.py
```

Outputs:

- `analysis_results/milestone3_fb.png`
- `analysis_results/milestone3_books.png`
- `analysis_results/milestone3_osmc.png`
- `analysis_results/milestone3_summary.csv`

The analysis script chooses the best row per index by average mixed throughput across the three repeat columns.

## Current Verification State

Checked locally:

- README requirements were reviewed.
- Hybrid source files exist and are wired into `CMakeLists.txt` and `benchmark.cc`.
- `build/benchmark --help` runs successfully, so a benchmark binary exists.
- No local `results/` or `analysis_results/` directories are present in this checkout, so previous benchmark numbers and plots are not available here.

Recommended next checks:

```bash
cd build
make -j4 benchmark
cd ..
```

Then, if data is available, run at least one small/manual benchmark with `--verify` before the full Slurm run. For final reporting, use `-r 3` in a single session as required by the README.

## Interpretation Notes for Report

- In the 90% insert workload, the best async hybrid is expected to be the lazy/no-flush configuration (`flush_interval = 2000000`). It avoids inserting 1.8M new keys into LIPP during the benchmark while still serving old-key lookups from LIPP.
- In the 10% insert workload, pure LIPP is expected to remain very difficult to beat. The hybrid adds DPGM checks for negative lookups and recently inserted positives, while LIPP already answers old-key positives efficiently.
- The hybrid trades memory for throughput. DynamicPGM is much more compact, while the hybrid stores almost the whole initial dataset in LIPP plus the DPGM write buffer.
- No auxiliary filters or maps are used in the current hybrid implementation, matching the assignment restriction.
