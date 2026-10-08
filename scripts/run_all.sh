#!/usr/bin/env bash
# Five repetitions of experiments A–E; fresh server volumes and embedded paths.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
RUNS=${RUNS:-5}
DURATION=${DURATION:-30}
ROWS=${ROWS:-1000000}
EXPERIMENTS=${EXPERIMENTS:-A,B,C,D,E}
DATABASES=${DATABASES:-rocksdb,leveldb,mysql,postgresql,cassandra}
RESULTS_ROOT=${RESULTS_ROOT:-"$ROOT/results/campaign_$(date -u +%Y%m%dT%H%M%SZ)"}
BENCHMARK_BIN=${BENCHMARK_BIN:-"$ROOT/build/latency_test"}
DRY_RUN=0
if [[ ${1:-} == --dry-run ]]; then DRY_RUN=1; elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--dry-run]" >&2; exit 2
fi
[[ $RUNS =~ ^[1-9][0-9]*$ && $DURATION =~ ^[1-9][0-9]*$ && $ROWS =~ ^[1-9][0-9]*$ ]] || {
    echo "RUNS, DURATION, and ROWS must be positive integers" >&2; exit 2;
}
IFS=, read -r -a selected_experiments <<< "$EXPERIMENTS"
IFS=, read -r -a selected_databases <<< "$DATABASES"
for experiment in "${selected_experiments[@]}"; do
    [[ $experiment =~ ^[ABCDE]$ ]] || { echo "Unknown experiment: $experiment" >&2; exit 2; }
done
for db in "${selected_databases[@]}"; do
    [[ $db =~ ^(rocksdb|leveldb|mysql|postgresql|cassandra)$ ]] || {
        echo "Unknown database: $db" >&2; exit 2;
    }
done
if (( ! DRY_RUN )); then
    [[ -x $BENCHMARK_BIN ]] || { echo "Build the benchmark first: $BENCHMARK_BIN is missing" >&2; exit 2; }
    if find src include -type f -newer "$BENCHMARK_BIN" -print -quit | grep -q .; then
        echo "Benchmark binary is older than the source; rebuild before running" >&2; exit 2
    fi
    command -v docker >/dev/null || { echo "docker is required" >&2; exit 2; }
    command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 2; }
    mkdir -p "$RESULTS_ROOT"
    [[ ! -e "$RESULTS_ROOT/manifest.tsv" ]] || { echo "Campaign exists: $RESULTS_ROOT" >&2; exit 2; }
    printf 'experiment\tconfiguration\trun\tdatabase\trows\tthreads\tpayload_bytes\tread_pct\twrite_pct\tdistribution\tresult_file\n' > "$RESULTS_ROOT/manifest.tsv"
    { date -u +'%Y-%m-%dT%H:%M:%SZ'; uname -a; git rev-parse HEAD 2>/dev/null || true;
      command -v lscpu >/dev/null && lscpu || true;
      command -v free >/dev/null && free -b || true;
      command -v lsblk >/dev/null && lsblk -b || true;
    } > "$RESULTS_ROOT/host.txt"
    cp docker-compose.yml "$RESULTS_ROOT/docker-compose.yml"
fi

wait_healthy() {
    local service status deadline=$((SECONDS + 900))
    for service in research_mysql research_postgres research_cassandra; do
        while (( SECONDS < deadline )); do
            status=$(docker inspect --format='{{.State.Health.Status}}' "$service" 2>/dev/null || true)
            [[ $status == healthy ]] && break
            if [[ $status == unhealthy ]]; then
                echo "$service is unhealthy" >&2; return 1
            fi
            sleep 5
        done
        [[ $status == healthy ]] || { echo "Timed out waiting for $service" >&2; return 1; }
    done
}

run_configuration() {
    local experiment=$1 config=$2 rows=$3 threads=$4 payload=$5 read_pct=$6 write_pct=$7 dist=$8 mode=$9
    local run db dir out
    for ((run=1; run<=RUNS; run++)); do
        dir="$RESULTS_ROOT/$experiment/$config/run_$(printf '%02d' "$run")"
        if (( DRY_RUN )); then
            printf '%s %s run %d: %s rows=%d threads=%d payload=%d read/write=%d/%d dist=%s mode=%s\n' \
                "$experiment" "$config" "$run" "$DATABASES" "$rows" "$threads" "$payload" "$read_pct" "$write_pct" "$dist" "$mode"
            continue
        fi
        mkdir -p "$dir"
        # Reset the benchmark Compose project's data before each repetition.
        docker compose down -v --remove-orphans > "$dir/docker-reset.log" 2>&1
        docker compose up -d > "$dir/docker-start.log" 2>&1
        wait_healthy
        docker compose ps > "$dir/docker-ps.txt"
        { docker inspect --format='{{.Name}} {{.Image}}' research_mysql research_postgres research_cassandra;
          docker exec research_mysql mysql --version;
          docker exec research_postgres postgres --version;
          docker exec research_cassandra cassandra -v;
        } > "$dir/server-versions.txt" 2>&1 || true
        for db in "${selected_databases[@]}"; do
            out="$dir/$db.json"
            port=()
            [[ $db == mysql ]] && port=(--port 3307)
            [[ $db == postgresql ]] && port=(--port 5433)
            dbname=()
            if [[ $db == rocksdb || $db == leveldb ]]; then
                dbname=(--dbname "$dir/${db}_data")
            fi
            args=(--db "$db" --rows "$rows" --threads "$threads" --payload-size "$payload"
                  --out "$out" "${port[@]}" "${dbname[@]}")
            if [[ $mode == chaos ]]; then
                args+=(--chaos)
            else
                args+=(--duration "$DURATION" --read-pct "$read_pct" --write-pct "$write_pct" --dist "$dist")
            fi
            printf '%q ' "$BENCHMARK_BIN" "${args[@]}" > "$dir/$db.command.txt"
            printf '\n' >> "$dir/$db.command.txt"
            echo "Running $experiment/$config repetition $run: $db"
            "$BENCHMARK_BIN" "${args[@]}" > "$dir/$db.stdout.log" 2> "$dir/$db.stderr.log"
            if [[ $db == rocksdb || $db == leveldb ]]; then
                du -sb "$dir/${db}_data" > "$dir/$db.storage-bytes.txt"
            elif [[ $db == mysql ]]; then
                docker exec research_mysql mysql -ubench -pbenchpass bench \
                    -e 'SHOW CREATE TABLE bench_kv' > "$dir/$db.schema.txt" 2>&1 || true
            elif [[ $db == postgresql ]]; then
                docker exec research_postgres psql -U bench -d bench \
                    -c '\d+ bench_kv' > "$dir/$db.schema.txt" 2>&1 || true
            elif [[ $db == cassandra ]]; then
                docker exec research_cassandra cqlsh \
                    -e 'DESCRIBE TABLE bench.bench_kv' > "$dir/$db.schema.txt" 2>&1 || true
            fi
            python3 scripts/validate_run.py "$out" "$db" "$payload" > "$dir/$db.validation.json"
            printf '%s\t%s\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n' \
                "$experiment" "$config" "$run" "$db" "$rows" "$threads" "$payload" \
                "$read_pct" "$write_pct" "$dist" "$out" >> "$RESULTS_ROOT/manifest.tsv"
        done
    done
}

for experiment in "${selected_experiments[@]}"; do
    case $experiment in
        A)
            for size in 10000 100000 500000 1000000; do
                run_configuration A "rows_${size}" "$size" 4 1024 70 30 uniform steady
            done ;;
        B)
            run_configuration B transition "$ROWS" 4 1024 90 10 uniform chaos ;;
        C)
            for threads in 1 4 16 64; do
                run_configuration C "threads_${threads}" "$ROWS" "$threads" 1024 10 90 zipfian steady
            done ;;
        D)
            for payload in 100 1024 10240; do
                run_configuration D "payload_${payload}" "$ROWS" 4 "$payload" 10 90 zipfian steady
            done ;;
        E)
            run_configuration E amplification "$ROWS" 4 1024 10 90 zipfian steady ;;
    esac
done
if (( ! DRY_RUN )); then
    docker compose down > "$RESULTS_ROOT/docker-stop.log" 2>&1
    echo "Raw results and manifest: $RESULTS_ROOT"
fi
