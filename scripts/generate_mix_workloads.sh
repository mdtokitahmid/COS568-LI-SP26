#!/usr/bin/env bash
# Regenerate the missing --mix ops files for books and osmc.
# Run on the login node: bash scripts/generate_mix_workloads.sh

set -e

cd "$(dirname "$0")/.."

GENERATE=build/generate
if [ ! -x "$GENERATE" ]; then
    echo "Error: $GENERATE not found. Build first."
    exit 1
fi

for DATASET in books_100M_public_uint64 osmc_100M_public_uint64; do
    echo "=== Generating mix workloads for $DATASET ==="

    echo "  10% insert mix..."
    $GENERATE ./data/$DATASET 2000000 --insert-ratio 0.1 --negative-lookup-ratio 0.5 --mix

    echo "  90% insert mix..."
    $GENERATE ./data/$DATASET 2000000 --insert-ratio 0.9 --negative-lookup-ratio 0.5 --mix

    echo "  Done: $DATASET"
done

echo ""
echo "=== Workload generation complete ==="
ls -lh data/books_100M_public_uint64_ops_2M_*mix data/osmc_100M_public_uint64_ops_2M_*mix 2>/dev/null
