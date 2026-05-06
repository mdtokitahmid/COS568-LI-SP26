#!/usr/bin/env bash
# Submit a single job: LIPP on FB dataset, 10% insert workload.
# Run from the project root: bash scripts/submit_lipp_fb_10pct.sh

set -e

PROJECT_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"
DATASET="fb_100M_public_uint64"
SUFFIX="ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
CSV="${PROJECT_DIR}/results/${DATASET}_${SUFFIX}_results_table.csv"

# Wipe only the LIPP rows from this CSV (or clear the whole file for a clean run)
> "$CSV"
echo "Cleared: results/${DATASET%%_*}_10pct CSV"

JOB_ID=$(sbatch --parsable \
    --job-name="lipp-fb-10pct" \
    --output="${PROJECT_DIR}/benchmark_%j.out" \
    --error="${PROJECT_DIR}/benchmark_%j.err" \
    --time=02:00:00 \
    --nodes=1 \
    --ntasks=1 \
    --cpus-per-task=2 \
    --mem=32G \
    --partition=cpu \
    --wrap="cd ${PROJECT_DIR} && build/benchmark \
        data/${DATASET} \
        data/${DATASET}_${SUFFIX} \
        --through --csv --only LIPP -r 3")

echo "Submitted job $JOB_ID — LIPP on FB 10% insert"
echo "Monitor: squeue -u \$USER"
