#!/usr/bin/env bash
# Submit ARIA on FB dataset for both workloads.
# Run from the project root: bash scripts/submit_aria_fb.sh

set -e

PROJECT_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"
DATASET="fb_100M_public_uint64"

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)
WORKLOAD_LABELS=("10pct" "90pct")

for i in "${!WORKLOAD_SUFFIXES[@]}"; do
    SUFFIX="${WORKLOAD_SUFFIXES[$i]}"
    LABEL="${WORKLOAD_LABELS[$i]}"
    CSV="${PROJECT_DIR}/results/${DATASET}_${SUFFIX}_results_table.csv"

    > "$CSV"
    echo "Cleared CSV for $LABEL"

    JOB_ID=$(sbatch --parsable \
        --job-name="aria-fb-${LABEL}" \
        --output="${PROJECT_DIR}/benchmark_%j.out" \
        --error="${PROJECT_DIR}/benchmark_%j.err" \
        --time=02:00:00 \
        --nodes=1 --ntasks=1 \
        --cpus-per-task=2 \
        --mem=32G \
        --partition=cpu \
        --wrap="cd ${PROJECT_DIR} && build/benchmark \
            data/${DATASET} \
            data/${DATASET}_${SUFFIX} \
            --through --csv --only ARIA -r 3")

    echo "Submitted aria-fb-${LABEL} -> job $JOB_ID"
done

echo ""
echo "Monitor: squeue -u \$USER"
