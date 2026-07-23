#!/bin/bash
set -e

echo "================================================="
echo "Restarting Database Services..."
echo "================================================="
sudo systemctl restart mysql || true
sudo systemctl restart postgresql || true
sudo systemctl restart cassandra || true

echo "Waiting for services to initialize..."
sleep 15

ROWS=10000000
RUNS=3

echo "================================================="
echo "Running Benchmarks (rows=$ROWS, chaos mode)"
echo "================================================="

DBS=("rocksdb" "leveldb" "mysql" "postgresql" "cassandra")

for run in $(seq 1 $RUNS); do
    echo "================================================="
    echo "Starting Iteration $run / $RUNS"
    echo "================================================="
    
    for db in "${DBS[@]}"; do
        echo ">>> Benchmarking $db (Run $run)"
        
        PORT_FLAG=""
        if [ "$db" = "mysql" ]; then
            PORT_FLAG="--port 3306"
        elif [ "$db" = "postgresql" ]; then
            PORT_FLAG="--port 5432"
        elif [ "$db" = "cassandra" ]; then
            PORT_FLAG="--port 9042"
        fi
        
        # Start dstat telemetry
        CSV_FILE="telemetry_${db}_run${run}.csv"
        echo "Starting dstat logging to $CSV_FILE"
        dstat --time --cpu --mem --disk --io --net --output "$CSV_FILE" 1 > /dev/null &
        DSTAT_PID=$!
        
        # Run Benchmark
        ./build/latency_test --db "$db" --chaos --rows $ROWS $PORT_FLAG --out "results_${db}_run${run}.json"
        
        # Stop dstat telemetry
        echo "Stopping dstat logging (PID: $DSTAT_PID)"
        kill $DSTAT_PID || true
        wait $DSTAT_PID 2>/dev/null || true
    done
done

echo "================================================="
echo "Generating Plots..."
echo "================================================="
python3 scripts/plot_results.py --results-dir . --output-dir plots

echo "Done! Check the 'plots' directory for the graphs."
