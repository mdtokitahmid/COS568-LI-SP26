#!/usr/bin/env bash
# Milestone 3: Run async hybrid benchmark on all 3 datasets.
# Compares DynamicPGM, LIPP, HybridPGMLIPP, HybridPGMLIPPAsync
# on both mixed workloads (10% insert and 90% insert).

set -e

BENCHMARK=build/benchmark
if [ ! -f $BENCHMARK ]; then
    echo "Error: benchmark binary not found. Run 'cd build && make -j4 benchmark' first."
    exit 1
fi

DATA_DIR=./data
RESULTS_DIR=./results
mkdir -p $RESULTS_DIR

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

WORKLOAD_SUFFIXES=(
    "ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
    "ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
)

for DATASET in "${DATASETS[@]}"; do
    DATA_FILE="${DATA_DIR}/${DATASET}"
    if [ ! -f "$DATA_FILE" ]; then
        echo "Warning: dataset $DATA_FILE not found, skipping."
        continue
    fi

    for SUFFIX in "${WORKLOAD_SUFFIXES[@]}"; do
        OPS_FILE="${DATA_DIR}/${DATASET}_${SUFFIX}"
        if [ ! -f "$OPS_FILE" ]; then
            echo "Warning: workload $OPS_FILE not found, skipping."
            continue
        fi

        echo "=== Dataset: $DATASET | Workload: $SUFFIX ==="

        for INDEX in DynamicPGM LIPP HybridPGMLIPP HybridPGMLIPPAsync; do
            echo "  Running $INDEX ..."
            $BENCHMARK $DATA_FILE $OPS_FILE \
                --through --csv --only $INDEX -r 3
        done
    done
done

echo ""
echo "=== Done. Results written to $RESULTS_DIR ==="

# Ensure headers are set on all mix result files
for FILE in ${RESULTS_DIR}/*mix*results_table.csv; do
    [ -f "$FILE" ] || continue
    if ! head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
        echo "Header set for $FILE"
    fi
done
