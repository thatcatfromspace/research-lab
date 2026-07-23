#!/bin/bash
set -e

ROWS=${1:-10000000}
RUNS=${2:-3}

echo "================================================="
echo "Starting Full EC2 Benchmark Suite (rows=$ROWS, runs=$RUNS)"
echo "================================================="

bash scripts/run_rocksdb.sh "$ROWS" "$RUNS"
bash scripts/run_leveldb.sh "$ROWS" "$RUNS"
bash scripts/run_mysql.sh "$ROWS" "$RUNS"
bash scripts/run_postgresql.sh "$ROWS" "$RUNS"
bash scripts/run_cassandra.sh "$ROWS" "$RUNS"

echo "================================================="
echo "Generating Plots..."
echo "================================================="
python3 scripts/plot_results.py --results-dir . --output-dir plots

echo "Done! Check the 'plots' directory for the graphs."
