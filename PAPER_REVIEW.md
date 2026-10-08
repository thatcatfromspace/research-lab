# Paper revision recommendations

This document separates proposed changes from results reported in `paper.tex`. The repository currently contains 15 workload-shift JSON files: three runs for each of five configurations. The author can supply raw files for the other original experiments; their results remain open for assessment and are not treated as disproven or permanently excluded.

## Evidence audit of the original experimental scope

| Experiment | Files currently in repository | Assessment |
| --- | --- | --- |
| A: cache-capacity scaling | No matched row-count sweep outputs | Method can be run with `--rows`, but numerical claims await raw per-configuration runs. |
| B: Workload Shifter | Three JSON runs and telemetry CSVs per configuration | Pilot phase traces are analyzable as **returned adapter calls**. Successful database operations are not recorded. |
| C: concurrency scaling | No matched thread-count sweep outputs | Harness supports `--threads`; TER values need measured one-worker and multiworker runs. |
| D: payload sensitivity | No matched payload-size sweep outputs | Harness supports `--payload-size`; recorded values are repeated characters, so compression must be considered. |
| E: amplification | End-of-run engine snapshots and mostly zero amplification fields | Cannot derive comparable WAF/SAF without logical operation counts and before/after physical-byte measurements. |

The PostgreSQL adapter can create `VARCHAR(255)` values if the table is absent, while `setup_ec2.sh` creates `TEXT` beforehand. The archived run files do not reveal the effective schema. PostgreSQL's almost-zero updated-tuple counters and large rollback counts suggest that writes may have failed, but do not identify why. The adapters discard per-operation status, so the five-system call-rate table is **not** a ranking of verified successful database throughput. Any rerun should fix status accounting before collecting additional data.

## Replication and the proposed five-run target

Two extra runs cannot be interpolated from the three existing runs. Interpolation may smooth a plot within a measured run; it cannot measure independent run-to-run variability, add degrees of freedom, or reveal a rare compaction or latency event. Resampling the same three runs also cannot substitute for new runs. The current paper reports the three observations and a wide descriptive Student-*t* interval where appropriate.

If five runs are a submission requirement, provision a new instance and collect a fresh, documented campaign. Prefer five independent runs **per configuration**, with randomized order, explicit reset or warm-state policy, version and durability manifests, and operation-success counts. Treat the new campaign as the primary dataset. Pooling it with the old runs would require matching the old instance, engine versions, dataset preparation, memory and storage settings, and benchmark code; the archived JSON files lack much of that metadata. A changed instance type is a new experimental block, not two more replicates of the old one.

## Literature positioning

1. **Dynamic benchmarks are prior work.** [Dyn-YCSB](https://repository.lsu.edu/enviro_sciences_pubs/388/) varies YCSB workload parameters over time. The manuscript now cites it and frames its contribution as a cross-system recovery analysis rather than the invention of dynamic workload benchmarking. A broader revision should compare the precise transition schedule, measured outcomes, and system scope against Dyn-YCSB and the [original YCSB paper](https://www.cs.albany.edu/~jhh/courses/readings/cooper.socc10.benchmarking.pdf).
2. **LSM mechanisms need qualified discussion.** [Monkey](https://scholar.harvard.edu/files/stratos/files/monkeykeyvaluestore.pdf) analyzes the trade-off among merge policy, Bloom filters, and memory. [Dostoevsky](https://scholar.harvard.edu/files/stratos/files/dostoyevski.pdf) discusses trade-offs across workload transitions and lookup skew. These sources support mechanism hypotheses, not attribution of the observed RocksDB deficit without synchronized compaction and I/O evidence.
3. **Skew and dynamic compaction deserve comparison.** [F2](https://arxiv.org/abs/2305.01516) studies large skewed key-value workloads; [ArceKV](https://doi.org/10.14778/3796195.3796208) studies workload-driven LSM compaction under dynamic workloads. They can sharpen the discussion if the paper's central question remains recovery after a skewed write burst.
4. **Latency methodology matters.** The benchmark uses a closed-loop worker model. [Friedrich, Wingerath, and Ritter's coordinated-omission study](https://vsis-www.informatik.uni-hamburg.de/getDoc.php/publications/569/Coordinated_Omission_in_NoSQL_Database_Benchmarking-Friedrich.pdf) motivates a rate-controlled companion experiment before interpreting P99 as an offered-load service-level result.

## Experiments that would support stronger claims

| Claim to test | Minimum experiment | Critical measurement |
| --- | --- | --- |
| Recovery is caused by the write fraction or key skew | Three-phase runs varying one factor at a time, plus a no-shift control | Exact boundaries, operation-specific latency, cache and compaction traces |
| A system reaches a cache-capacity cliff | Sweep measured working-set bytes relative to effective memory, with cold and warm blocks | Cache misses, device reads, memory accounting, throughput and P99 |
| Concurrency is limited by a particular mechanism | Matched 1, 4, 16, 64-worker runs | Lock waits, stalls, CPU, queue depth and compaction activity |
| Payload size changes write amplification | Payload sweep with compressible and incompressible values | Successful logical bytes and device writes through a drain period |
| One architecture has lower amplification | Common before/after accounting on a bounded workload | WAL, data-file, flush, compaction and device bytes; live logical bytes |

The first experiment is the most direct extension of the current manuscript. It separates two factors that the existing stress phase changes simultaneously and provides a no-shift baseline for temporal drift. The other sweeps can become a broader paper once measured; adding their unrun numerical claims to the present manuscript would weaken it.

## Before another AWS campaign

The repository contains EC2 setup and per-engine scripts, but the old instance is gone and the result files do not embed their exact commands or engine versions. The next campaign should save a machine-readable manifest for every run, randomize execution order, and validate successful operation counts and phase timestamps. No new AWS run was started as part of this paper revision.
