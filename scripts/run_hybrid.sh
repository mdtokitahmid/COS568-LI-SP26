#!/usr/bin/env bash
# Run the hybrid benchmark on the Facebook dataset for Milestone 2.
# Compares HybridPGMLIPP against DynamicPGM and LIPP on the two mixed workloads:
#   - 90% insert / 10% lookup
#   - 10% insert / 90% lookup

set -e

BENCHMARK=build/benchmark
if [ ! -f $BENCHMARK ]; then
    echo "Error: benchmark binary not found. Run scripts/build_benchmark.sh first."
    exit 1
fi

DATASET=fb_100M_public_uint64
DATA_DIR=./data
RESULTS_DIR=./results
mkdir -p $RESULTS_DIR

WORKLOADS=(
    "${DATASET}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix"
    "${DATASET}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix"
)

for WORKLOAD in "${WORKLOADS[@]}"; do
    OPS_FILE="${DATA_DIR}/${WORKLOAD}"
    if [ ! -f "$OPS_FILE" ]; then
        echo "Warning: workload file $OPS_FILE not found, skipping."
        continue
    fi

    echo "=== Workload: $WORKLOAD ==="

    for INDEX in DynamicPGM LIPP HybridPGMLIPP; do
        echo "  Running $INDEX ..."
        $BENCHMARK ${DATA_DIR}/${DATASET} ${OPS_FILE} \
            --through --csv --only $INDEX -r 3
    done
done

echo ""
echo "=== Done. Results written to $RESULTS_DIR ==="

# Add CSV headers for the mix result files
for FILE in ${RESULTS_DIR}/${DATASET}*mix*results_table.csv; do
    [ -f "$FILE" ] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "Header set for $FILE"
done
