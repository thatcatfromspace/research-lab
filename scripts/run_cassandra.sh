#!/bin/bash
set -e

ROWS=${1:-10000000}
RUNS=${2:-3}
PORT=${3:-9042}

echo "================================================="
echo "Restarting Cassandra Service..."
echo "================================================="
sudo systemctl restart cassandra || true
echo "Waiting 15s for Cassandra to initialize..."
sleep 15

echo "================================================="
echo "Benchmarking Cassandra (rows=$ROWS, runs=$RUNS, port=$PORT)"
echo "================================================="

for run in $(seq 1 $RUNS); do
    echo ">>> Benchmarking Cassandra (Run $run / $RUNS)"
    
    CSV_FILE="telemetry_cassandra_run${run}.csv"
    echo "Starting dstat logging to $CSV_FILE"
    dstat --time --cpu --mem --disk --io --net --output "$CSV_FILE" 1 > /dev/null &
    DSTAT_PID=$!
    
    ./build/latency_test --db cassandra --chaos --rows "$ROWS" --port "$PORT" --out "results_cassandra_run${run}.json"
    
    echo "Stopping dstat logging (PID: $DSTAT_PID)"
    kill $DSTAT_PID || true
    wait $DSTAT_PID 2>/dev/null || true
done

echo "Cassandra benchmark complete."
