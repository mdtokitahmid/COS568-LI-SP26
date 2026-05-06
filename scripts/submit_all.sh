#!/usr/bin/env bash
# Submit one sbatch job per (dataset, workload) pair — 6 jobs total.
# Each job runs all 4 methods sequentially so only one process writes
# to each CSV file at a time (no concurrent append corruption).
# CSV files are wiped clean before submission.
#
# Run from the project root: bash scripts/submit_all.sh

set -e

PROJECT_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)

WORKLOAD_LABELS=(
    "10pct"
    "90pct"
)

# ── Wipe all 6 CSV files before submitting ───────────────────────────────────
echo "Initializing CSV files..."
for DATASET in "${DATASETS[@]}"; do
    for SUFFIX in "${WORKLOAD_SUFFIXES[@]}"; do
        CSV="${PROJECT_DIR}/results/${DATASET}_${SUFFIX}_results_table.csv"
        > "$CSV"
        echo "  Cleared: ${DATASET%%_*} / ${SUFFIX##*nl_}"
    done
done
echo ""

# ── Submit 6 jobs ─────────────────────────────────────────────────────────────
TOTAL=$(( ${#DATASETS[@]} * ${#WORKLOAD_SUFFIXES[@]} ))
echo "Submitting $TOTAL jobs (1 per dataset × workload)..."
echo ""

for DATASET in "${DATASETS[@]}"; do
    DS_SHORT="${DATASET%%_*}"
    for i in "${!WORKLOAD_SUFFIXES[@]}"; do
        SUFFIX="${WORKLOAD_SUFFIXES[$i]}"
        LABEL="${WORKLOAD_LABELS[$i]}"
        JOB_NAME="m3-${DS_SHORT}-${LABEL}"

        JOB_ID=$(sbatch --parsable \
            --job-name="$JOB_NAME" \
            --output="${PROJECT_DIR}/benchmark_%j.out" \
            --error="${PROJECT_DIR}/benchmark_%j.err" \
            --time=04:00:00 \
            --nodes=1 \
            --ntasks=1 \
            --cpus-per-task=2 \
            --mem=32G \
            --partition=cpu \
            --wrap="cd ${PROJECT_DIR} && bash scripts/run_all_methods.sh ${DATASET} ${SUFFIX}")

        echo "  Submitted $JOB_NAME -> job $JOB_ID"
    done
done

echo ""
echo "All $TOTAL jobs submitted. Monitor with: squeue -u \$USER"
echo "Once all complete, run: python scripts/analysis_m3.py"
