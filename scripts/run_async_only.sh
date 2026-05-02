#!/usr/bin/env bash
# Run ONLY ARIA on the FB dataset.
# DynamicPGM, LIPP, and HybridPGMLIPP results are left unchanged.

set -e

cd "$(dirname "$0")/.."

BENCHMARK=build/benchmark
DATA_DIR=./data
RESULTS_DIR=./results
mkdir -p $RESULTS_DIR

DATASET="fb_100M_public_uint64"

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)

DATA_FILE="${DATA_DIR}/${DATASET}"
if [ ! -f "$DATA_FILE" ]; then
    echo "Error: dataset $DATA_FILE not found."
    exit 1
fi

for SUFFIX in "${WORKLOAD_SUFFIXES[@]}"; do
    OPS_FILE="${DATA_DIR}/${DATASET}_${SUFFIX}"
    BULKLOAD_FILE="${OPS_FILE}_bulkload"
    if [ ! -s "$OPS_FILE" ]; then
        echo "Error: workload $OPS_FILE missing or empty."
        exit 1
    fi
    if [ ! -s "$BULKLOAD_FILE" ]; then
        echo "Error: bulkload $BULKLOAD_FILE missing or empty."
        exit 1
    fi

    echo "=== Dataset: $DATASET | Workload: $SUFFIX ==="
    echo "  Running ARIA ..."
    $BENCHMARK $DATA_FILE $OPS_FILE \
        --through --csv --only ARIA -r 3
done

echo ""
echo "=== Done. ARIA results written to $RESULTS_DIR ==="
