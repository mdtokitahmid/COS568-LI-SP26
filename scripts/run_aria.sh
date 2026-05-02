#!/usr/bin/env bash
# Run ARIA on all three datasets for both mix workloads.
# Usage: bash scripts/run_async.sh
# Run from the project root.

set -e

BENCHMARK=build/benchmark
if [ ! -f "$BENCHMARK" ]; then
    echo "Error: benchmark binary not found. Run 'cd build && make -j4 benchmark' first."
    exit 1
fi

INDEX="ARIA"
DATA_DIR=./data
RESULTS_DIR=./results
mkdir -p "$RESULTS_DIR"

DATASETS=(
    # "fb_100M_public_uint64"
    "books_100M_public_uint64"
    # "osmc_100M_public_uint64"
)

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    # "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)

for DATASET in "${DATASETS[@]}"; do
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

        echo "=== Dataset: $DATASET | Index: $INDEX | Workload: $SUFFIX ==="
        $BENCHMARK "$DATA_FILE" "$OPS_FILE" \
            --through --csv --only "$INDEX" -r 3
    done
done

echo ""
echo "=== Done: ARIA on all datasets ==="
