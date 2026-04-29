#!/bin/bash
#SBATCH --job-name=cos568-hybrid
#SBATCH --output=benchmark_%j.out
#SBATCH --error=benchmark_%j.err
#SBATCH --time=02:00:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --partition=cpu

cd /scratch/gpfs/MONA/Toki/Academic/COS568/COS568-LI-SP26

BENCHMARK=build/benchmark
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
    echo "=== Workload: $WORKLOAD ==="

    for INDEX in DynamicPGM LIPP HybridPGMLIPP; do
        echo "  Running $INDEX ..."
        $BENCHMARK ${DATA_DIR}/${DATASET} ${OPS_FILE} \
            --through --csv --only $INDEX -r 3
    done
done

echo "=== Done ==="

# Add CSV headers
for FILE in ${RESULTS_DIR}/${DATASET}*mix*results_table.csv; do
    [ -f "$FILE" ] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
done
