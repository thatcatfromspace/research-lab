#!/bin/bash
set -e

ROWS=${1:-10000000}
RUNS=${2:-3}
PORT=${3:-5432}

echo "================================================="
echo "Restarting PostgreSQL Service..."
echo "================================================="
sudo systemctl restart postgresql || true
sleep 5

echo "================================================="
echo "Benchmarking PostgreSQL (rows=$ROWS, runs=$RUNS, port=$PORT)"
echo "================================================="

for run in $(seq 1 $RUNS); do
    echo ">>> Benchmarking PostgreSQL (Run $run / $RUNS)"
    
    CSV_FILE="telemetry_postgresql_run${run}.csv"
    echo "Starting dstat logging to $CSV_FILE"
    dstat --time --cpu --mem --disk --io --net --output "$CSV_FILE" 1 > /dev/null &
    DSTAT_PID=$!
    
    ./build/latency_test --db postgresql --chaos --rows "$ROWS" --port "$PORT" --out "results_postgresql_run${run}.json"
    
    echo "Stopping dstat logging (PID: $DSTAT_PID)"
    kill $DSTAT_PID || true
    wait $DSTAT_PID 2>/dev/null || true
done

echo "PostgreSQL benchmark complete."
