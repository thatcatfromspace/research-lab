# Five-run benchmark campaign

Build the C++ benchmark with the dependencies in `CMakeLists.txt`, then run
`bash scripts/run_all.sh` from this repository. Use
`bash scripts/run_all.sh --dry-run` to inspect the matrix without starting
Docker. The default campaign has 13 configurations, five repetitions per
configuration, and five database results per repetition (325 raw JSON files).
`RUNS`, `DURATION`, `ROWS`, `EXPERIMENTS`, `DATABASES`, and
`RESULTS_ROOT` are environment overrides. For example:

```bash
EXPERIMENTS=B,C RUNS=5 RESULTS_ROOT="$PWD/results/aws_2026_10" bash scripts/run_all.sh
```

Each repetition starts fresh Docker Compose benchmark volumes and uses new
RocksDB/LevelDB directories. **Running the campaign deletes the benchmark
Compose project's existing volumes.** Keep other data outside those volumes.
The runs remain consecutive on one host; the runner does not clear the OS page
cache or make cache states statistically independent.
The runner writes each raw result, stdout/stderr, exact command, validation,
server version, host metadata, and a manifest under `RESULTS_ROOT`. Failed
operations stop the campaign. Results already written are kept for inspection.

| Experiment | Independent variable | Workload |
| --- | --- | --- |
| A | 10k, 100k, 500k, 1M seeded rows | 4 threads, 70/30 read/write, uniform |
| B | Three 10 s phases | 4 threads, 90/10 uniform → 10/90 Zipfian → 90/10 uniform |
| C | 1, 4, 16, 64 threads | `ROWS` seeded rows, 10/90 Zipfian |
| D | 100, 1,024, 10,240 byte values | `ROWS` seeded rows, 4 threads, 10/90 Zipfian |
| E | Write-heavy observation | `ROWS` seeded rows, 4 threads, 1,024-byte values, 10/90 Zipfian |

All steady workloads default to 30 s. The JSON includes per-operation
attempt/success/failure counts, one-second samples, exact phase boundaries,
latencies, logical successful write bytes, adapter metrics before and after
the timed workload, and Linux process I/O counters before and after. The
`.validation.json` file preserves selected counter deltas. A–D can therefore
be analysed at run level, including throughput elasticity and recovery-phase
throughput.

Experiment E collects **diagnostics**, not a comparable write amplification
factor. MySQL data-file and redo counters, PostgreSQL WAL counters, embedded
process I/O, and engine-specific size counters cover different storage
boundaries. Delayed writes can occur after the timed window; the live-data
denominator is not available on a common basis. The JSON reports
`amplification` as null until a consistent measurement protocol is added.
Likewise, ten seconds of the final phase measure throughput in that window;
they cannot establish time to full recovery.

The Compose setup is a local benchmark configuration. Its 64 MB MySQL buffer
pool and PostgreSQL shared buffers are database-managed cache settings, not
total memory limits. Runs on EC2 must record the actual machine, OS, engine
versions, storage, and build used; local Docker measurements must not be
presented as repetitions of the earlier EC2 campaign.
