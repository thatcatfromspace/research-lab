#!/bin/bash
set -e

echo "Cleaning up any old volumes..."
docker compose down -v

echo "Starting Docker containers..."
docker compose up -d

echo "Waiting for services to become healthy (this may take a minute for Cassandra)..."
for service in research_mysql research_postgres research_cassandra; do
    echo "Waiting for $service..."
    while true; do
        STATUS=$(docker inspect --format='{{json .State.Health.Status}}' $service 2>/dev/null || echo '"unknown"')
        if [ "$STATUS" = '"healthy"' ]; then
            echo "$service is healthy!"
            break
        elif [ "$STATUS" = '"unhealthy"' ]; then
            echo "Error: $service became unhealthy!"
            exit 1
        fi
        sleep 5
    done
done

echo "================================================="
echo "Running Benchmarks (chaos mode)"
echo "================================================="

# Array of databases
DBS=("rocksdb" "leveldb" "mysql" "postgresql" "cassandra")

for db in "${DBS[@]}"; do
    echo ">>> Benchmarking $db"
    PORT_FLAG=""
    if [ "$db" = "mysql" ]; then
        PORT_FLAG="--port 3307"
    elif [ "$db" = "postgresql" ]; then
        PORT_FLAG="--port 5433"
    fi
    ./build/latency_test --db "$db" --chaos $PORT_FLAG --out "results_${db}.json"
done

echo "================================================="
echo "Generating Plots..."
echo "================================================="
python3 scripts/plot_results.py --results-dir . --output-dir plots

echo "Stopping Docker containers..."
docker compose down

echo "Done! Check the 'plots' directory for the graphs."
