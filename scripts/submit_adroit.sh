#!/usr/bin/env bash
# Submit the FB-only Milestone 3 async benchmark job on Adroit.
#
# Usage:
#   bash scripts/submit_adroit.sh

set -euo pipefail

REPO_DIR="/home/mt3204/Academic/COS568/Project/COS568-LI-SP26"
cd "$REPO_DIR"

if [ ! -x build/benchmark ]; then
    echo "benchmark binary not found; building it first..."
    cmake --build build --target benchmark -j4
fi

echo "Submitting scripts/submit_async.sh from $REPO_DIR"
sbatch scripts/submit_async.sh
