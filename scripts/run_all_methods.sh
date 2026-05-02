#!/usr/bin/env bash
# Run ALL methods on a single (dataset, workload) pair, sequentially.
# Usage: bash scripts/run_all_methods.sh <DATASET> <WORKLOAD_SUFFIX>
# Called by submit_all.sh — one invocation per sbatch job.

set -e

DATASET="${1:?Usage: $0 <dataset> <workload_suffix>}"
SUFFIX="${2:?Usage: $0 <dataset> <workload_suffix>}"

BENCHMARK=build/benchmark
if [ ! -f "$BENCHMARK" ]; then
    echo "Error: benchmark binary not found. Run 'cd build && make -j4 benchmark' first."
    exit 1
fi

DATA_DIR=./data

DATA_FILE="${DATA_DIR}/${DATASET}"
if [ ! -f "$DATA_FILE" ]; then
    echo "Error: dataset $DATA_FILE not found."
    exit 1
fi

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

INDEXES=(
    "DynamicPGM"
    "LIPP"
    "HybridPGMLIPP"
    "ARIA"
)

for INDEX in "${INDEXES[@]}"; do
    echo "=== Dataset: $DATASET | Index: $INDEX | Workload: $SUFFIX ==="
    $BENCHMARK "$DATA_FILE" "$OPS_FILE" \
        --through --csv --only "$INDEX" -r 3
done

echo ""
echo "=== Done: all methods on $DATASET | $SUFFIX ==="
