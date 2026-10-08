#!/usr/bin/env python3
"""Check one raw benchmark result and retain measured counter deltas."""
import json
import sys
from pathlib import Path


def delta(before, after, key):
    a, b = before.get(key), after.get(key)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)) and b >= a:
        return b - a
    return None


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: validate_run.py RESULT.json DATABASE PAYLOAD_BYTES")
    path, db, payload = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
    raw = json.loads(path.read_text())
    counts = raw["operation_counts"]
    failures = 0
    for kind in ("read", "write", "scan"):
        c = counts[kind]
        if c["attempted"] != c["successful"] + c["failed"]:
            raise ValueError(f"{kind} counts do not add up")
        failures += c["failed"]
    if raw["logical_write_bytes"] != counts["write"]["successful"] * payload:
        raise ValueError("logical write bytes do not match successful writes")
    before, after = raw.get("db_metrics_before", {}), raw.get("db_metrics", {})
    proc_before, proc_after = raw.get("process_io_before", {}), raw.get("process_io_after", {})
    phase_rates = []
    for phase in raw.get("phases", []):
        elapsed = phase["end_time_s"] - phase["start_time_s"]
        phase_rates.append(phase["attempted"] / elapsed if elapsed > 0 else None)
    recovery_ratio = None
    if len(phase_rates) == 3 and phase_rates[0] and phase_rates[2] is not None:
        recovery_ratio = phase_rates[2] / phase_rates[0]
    diagnostic = {
        "database": db,
        "result_file": str(path),
        "failed_operations": failures,
        "logical_write_bytes": raw["logical_write_bytes"],
        "phase_throughput_ops": phase_rates,
        "recovery_phase_to_initial_ratio": recovery_ratio,
        "server_or_engine_counter_deltas": {
            key: delta(before, after, key)
            for key in (
                "mysql.Innodb_data_written",
                "mysql.Innodb_os_log_written",
                "mysql.Innodb_buffer_pool_reads",
                "mysql.Innodb_buffer_pool_read_requests",
                "postgres.wal_bytes",
                "postgres.blks_read",
                "postgres.blks_hit",
                "postgres.tup_updated",
                "postgres.xact_rollback",
            )
            if key in before or key in after
        },
        "client_process_io_deltas": {
            key: delta(proc_before, proc_after, key)
            for key in ("read_bytes", "write_bytes", "rchar", "wchar")
        },
        "write_amplification": None,
        "space_amplification": None,
        "note": (
            "Counters are raw diagnostics. Server WAL/data-file counters and "
            "embedded-process I/O have different boundaries; background writes "
            "may continue after the timed workload. No comparable physical "
            "write or live-data denominator is available for all five systems."
        ),
    }
    print(json.dumps(diagnostic, indent=2))
    if failures:
        print(f"{failures} failed operations in {path}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
