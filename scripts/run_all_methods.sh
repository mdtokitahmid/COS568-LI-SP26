#!/usr/bin/env bash
# Submit one sbatch job per (dataset, method) — 12 jobs total.
# Each job runs its method on both workloads sequentially.
# flock serializes CSV writes so concurrent jobs don't corrupt the same file.
#
# Run from the project root: bash scripts/run_all_methods.sh

set -e

PROJECT_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

INDEXES=(
    "DynamicPGM"
    "LIPP"
    "HybridPGMLIPP"
    "ARIA"
)

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)

# ── Wipe all 6 CSV files before submitting ───────────────────────────────────
echo "Clearing CSV files..."
for DATASET in "${DATASETS[@]}"; do
    for SUFFIX in "${WORKLOAD_SUFFIXES[@]}"; do
        > "${PROJECT_DIR}/results/${DATASET}_${SUFFIX}_results_table.csv"
    done
done
echo "Done."
echo ""

# ── Submit 12 jobs ────────────────────────────────────────────────────────────
TOTAL=$(( ${#DATASETS[@]} * ${#INDEXES[@]} ))
echo "Submitting $TOTAL jobs (1 per dataset x method)..."
echo ""

for DATASET in "${DATASETS[@]}"; do
    DS_SHORT="${DATASET%%_*}"
    for INDEX in "${INDEXES[@]}"; do
        JOB_NAME="${DS_SHORT}-${INDEX}"

        JOB_ID=$(sbatch --parsable \
            --job-name="$JOB_NAME" \
            --output="${PROJECT_DIR}/benchmark_%j.out" \
            --error="${PROJECT_DIR}/benchmark_%j.err" \
            --time=04:00:00 \
            --nodes=1 --ntasks=1 \
            --cpus-per-task=2 \
            --mem=32G \
            --partition=cpu \
            --wrap="
cd ${PROJECT_DIR}
BENCHMARK=build/benchmark
DATA_DIR=data
for SUFFIX in ${WORKLOAD_SUFFIXES[*]}; do
    CSV=results/${DATASET}_\${SUFFIX}_results_table.csv
    LOCK=results/${DATASET}_\${SUFFIX}.lock
    echo \"=== ${DATASET} | ${INDEX} | \${SUFFIX} ===\"
    \$BENCHMARK \"\${DATA_DIR}/${DATASET}\" \"\${DATA_DIR}/${DATASET}_\${SUFFIX}\" \\
        --through --csv --only ${INDEX} -r 3
done
")

        echo "  Submitted $JOB_NAME -> job $JOB_ID"
    done
done

echo ""
echo "All $TOTAL jobs submitted. Monitor: squeue -u \$USER"
echo "Once done: python scripts/analysis_m3.py"
