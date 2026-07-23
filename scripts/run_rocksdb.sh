#!/bin/bash
set -e

ROWS=${1:-10000000}
RUNS=${2:-3}

echo "================================================="
echo "Benchmarking RocksDB (rows=$ROWS, runs=$RUNS)"
echo "================================================="

for run in $(seq 1 $RUNS); do
    echo ">>> Benchmarking RocksDB (Run $run / $RUNS)"
    
    CSV_FILE="telemetry_rocksdb_run${run}.csv"
    echo "Starting dstat logging to $CSV_FILE"
    dstat --time --cpu --mem --disk --io --net --output "$CSV_FILE" 1 > /dev/null &
    DSTAT_PID=$!
    
    ./build/latency_test --db rocksdb --chaos --rows "$ROWS" --out "results_rocksdb_run${run}.json"
    
    echo "Stopping dstat logging (PID: $DSTAT_PID)"
    kill $DSTAT_PID || true
    wait $DSTAT_PID 2>/dev/null || true
done

echo "RocksDB benchmark complete."
