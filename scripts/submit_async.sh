#!/usr/bin/env bash
# Submit one sbatch job per (dataset, index) pair — all three datasets.
# Run from the project root: bash scripts/submit_async.sh

set -e

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

INDEXES=(
    "ARIA"
)

PROJECT_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"

echo "Submitting jobs for ${#DATASETS[@]} datasets x ${#INDEXES[@]} indexes = $((${#DATASETS[@]} * ${#INDEXES[@]})) jobs"
echo ""

for DATASET in "${DATASETS[@]}"; do
    for INDEX in "${INDEXES[@]}"; do
        DS_SHORT="${DATASET%%_*}"   # "books" or "osmc"
        JOB_NAME="m3-${DS_SHORT}-${INDEX}"

        MEM="32G"

        JOB_ID=$(sbatch --parsable \
            --job-name="$JOB_NAME" \
            --output="${PROJECT_DIR}/benchmark_%j.out" \
            --error="${PROJECT_DIR}/benchmark_%j.err" \
            --time=04:00:00 \
            --nodes=1 \
            --ntasks=1 \
            --cpus-per-task=2 \
            --mem="$MEM" \
            --partition=cpu \
            --wrap="cd ${PROJECT_DIR} && bash scripts/run_async.sh ${DATASET} ${INDEX}")

        echo "  Submitted $JOB_NAME (${MEM}) -> job $JOB_ID"
    done
done

echo ""
echo "All jobs submitted. Monitor with: squeue -u \$USER"
