# KubeSched — plan and progress

## Goal

Turn the original single-resource request scheduler into a Kubernetes-style pod scheduler that backs these three claims:

1. A Filter–Score scheduling engine for CPU- and memory-aware pod placement across heterogeneous nodes.
2. Bin-packing, spreading, and priority-based preemption policies for resource-constrained workloads.
3. Evaluation of the policies under varied workload patterns, using utilization, waiting time, and unschedulable-rate metrics.

## Plan

| # | Item | Status |
|---|------|--------|
| 1 | Resource model: CPU (millicores) + memory (MiB) on nodes and pods, per-node `maxPods` | done |
| 2 | Filter plugins: `ResourceFit` (CPU and memory), `PodCount` | done |
| 3 | Score plugins: `MostAllocated` (bin-pack), `LeastAllocated` (spread), `BalancedAllocation` | done |
| 4 | Policies built from plugins: `binpack`, `spread`, `balanced` (LeastAllocated + Balanced, the Kubernetes default) | done |
| 5 | Discrete-event simulator: arrivals, completions, pending-queue timeouts | done |
| 6 | Priority queue ordering (priority desc, then arrival) | done |
| 7 | Priority-based preemption with minimal-victim selection | done |
| 8 | Metrics: utilization (arrival window and full makespan), active nodes, average/p95/per-priority waiting, turnaround, unschedulable rate, delayed pods, preemptions, lost work | done |
| 9 | Heterogeneous cluster and seeded workload generator (`steady`, `bursty`, `mixed`, `overload`) | done |
| 10 | Evaluation harness: every policy × {no preemption, preemption} × every pattern, averaged over seeds, plus CSV output | done |
| 11 | CLI: `run`, `gen`, `eval` | done |
| 12 | Unit tests (`make test`) | done (14 test groups) |
| 13 | Makefile, `.gitignore`, sample `input.txt` | done |

## Architecture

| File | Role |
|------|------|
| [types.h](types.h) | `Resources`, `Node`, `Pod`, `ScheduleEvent`, `SimulationConfig`, `SimulationResult` |
| [policy.h](policy.h) / [policy.cpp](policy.cpp) | `FilterPlugin` / `ScorePlugin` interfaces, the plugins, and `SchedulingPolicy::selectNode` (filter → weighted score → best node) |
| [scheduler.h](scheduler.h) / [scheduler.cpp](scheduler.cpp) | Event-driven `Simulator`: admission, scheduling pass, preemption, timeouts, metric accounting |
| [workload.h](workload.h) / [workload.cpp](workload.cpp) | 12-node heterogeneous cluster and the seeded workload patterns |
| [eval.h](eval.h) / [eval.cpp](eval.cpp) | Runs the policy × pattern × seed grid and averages the results |
| [parser.h](parser.h) / [parser.cpp](parser.cpp) | Text input format, read and write |
| [report.h](report.h) / [report.cpp](report.cpp) | Event trace, metrics summary, comparison table, CSV |
| [main.cpp](main.cpp) | CLI |
| [tests.cpp](tests.cpp) | Unit tests |

## Design decisions

- **Filter → Score.** Every filter must pass. Each scorer returns a value in [0, 100], scorers are combined as a weighted mean, and ties go to the lowest node index. Scores are computed on the node state *after* the pod would be placed, as in kube-scheduler.
- **Scorers.**
  - `MostAllocated` = mean of the CPU and memory used fractions. It packs pods onto the fullest node.
  - `LeastAllocated` = mean of the free fractions. It spreads pods out.
  - `BalancedAllocation` = 1 − |cpuFrac − memFrac|. It avoids stranding one resource.
- **Admission.** A pod that does not fit on any node, even when that node is empty, is rejected at arrival as unschedulable. It does not wait forever.
- **Pending queue.** Pods are ordered by priority (highest first), then arrival time, then id. Each scheduling pass tries every pending pod, so a small pod is not blocked behind a large one that cannot fit (no head-of-line blocking).
- **Preemption** (a simplified version of kube-scheduler's DefaultPreemption). It runs only when a pod passes no filter on any node. For each node:
  1. Take the running pods with strictly lower priority as candidates.
  2. Check that the pod fits once all candidates are removed.
  3. Add candidates back, most important first (higher priority, then older), keeping each one the pod still fits around. The rest become victims.
  4. Pick the node whose victims have the lowest (max priority, sum of priorities, count).

  Victims are requeued and restart from scratch; the discarded run time is reported as `lostWork`. The preemptor binds to that node immediately.
- **Timeouts.** With `--timeout T`, a pod that stays pending for T continuously is dropped and counted as unschedulable. The eval default is 300.
- **Unschedulable rate** = (rejected at admission + timed out) / total pods.
- **Utilization** is time-weighted allocated / capacity. The headline figure covers the *arrival window* (first to last arrival). The full-makespan figure is diluted by the drain after the last arrival, which took half the run in `overload`, so it is kept as a secondary CSV column.
- **Reproducibility.** The generator uses `mt19937_64`, whose output the standard fixes, with hand-written distributions, because the `std::` distributions differ between libstdc++ and libc++. All policies in a pattern see identical workloads.

## Workload patterns

Offered load is computed on the busier resource (`targetLoad`), and the arrival rate is set from the sampled pod sizes.

| Pattern | Shape mix (web / cpu-heavy / mem-heavy / oversized) | Target load | Durations | Arrivals |
|---------|------|------|------|------|
| steady | 70 / 15 / 15 / 0 | 0.6 | exponential, mean 60 | Poisson |
| bursty | 70 / 15 / 15 / 0 | 0.6 | exponential, mean 60 | batches of 10–40 pods |
| mixed | 40 / 30 / 29 / 1 | 0.8 | lognormal (median 30, σ=1) | Poisson |
| overload | 40 / 30 / 29 / 1 | 1.3 | lognormal (median 30, σ=1) | Poisson |

Priorities: 10% critical (1000), 30% normal (100), 60% best-effort (0).
Cluster: 4× small (2 CPU / 4 GiB), 4× general (4 / 16), 2× highcpu (16 / 16), 2× highmem (4 / 64).

## Results snapshot (2026-10-26)

`./kubesched eval`: 500 pods per run, seeds 1–3, timeout 300. Utilization is over the arrival window.

| pattern | policy | cpu% | mem% | active nodes | avg wait | p95 wait | critical wait | unsched% | preemptions |
|---|---|---|---|---|---|---|---|---|---|
| steady | binpack | 55.7 | 30.6 | 6.2 | 3.3 | 7.7 | 1.4 | 0.0 | 0 |
| steady | spread | 56.1 | 30.6 | 10.1 | 1.7 | 4.3 | 0.7 | 0.0 | 0 |
| steady | balanced | 56.1 | 30.6 | 10.1 | 1.7 | 0.3 | 0.5 | 0.0 | 0 |
| steady | balanced+preempt | 58.0 | 31.0 | 10.2 | 2.6 | 2.7 | 0.0 | 0.0 | 11.7 |
| bursty | binpack | 60.2 | 34.2 | 8.4 | 8.5 | 51.0 | 2.3 | 0.5 | 0 |
| bursty | balanced | 60.7 | 34.4 | 9.5 | 7.8 | 45.0 | 2.0 | 0.3 | 0 |
| bursty | balanced+preempt | 65.0 | 35.8 | 9.6 | 10.2 | 56.0 | 0.0 | 0.4 | 43.3 |
| mixed | binpack | 65.4 | 44.0 | 5.6 | 15.0 | 123.3 | 2.1 | 1.7 | 0 |
| mixed | balanced | 65.1 | 44.1 | 8.2 | 12.2 | 103.7 | 3.4 | 1.7 | 0 |
| mixed | balanced+preempt | 68.5 | 44.9 | 8.4 | 14.6 | 126.7 | 0.0 | 3.3 | 46.7 |
| overload | binpack | 72.7 | 62.1 | 5.8 | 22.7 | 167.7 | 7.0 | 5.5 | 0 |
| overload | spread | 77.2 | 64.4 | 7.5 | 19.7 | 156.0 | 6.1 | 5.1 | 0 |
| overload | balanced | 75.7 | 64.6 | 7.5 | 20.1 | 152.7 | 6.8 | 5.1 | 0 |
| overload | balanced+preempt | 78.9 | 66.3 | 7.8 | 22.8 | 202.7 | 0.0 | 6.5 | 74.0 |

The full grid, all 6 configurations per pattern, is printed by `./kubesched eval` and written to `results.csv` by `make eval`.

Takeaways:
- **Bin-packing** keeps the same utilization on far fewer active nodes: 6.2 vs 10.1 in steady and 5.6 vs 8.2 in mixed, about 30–40% fewer. The cost is higher average and p95 waiting.
- **Spreading/balanced** gives the lowest waiting times, and the highest utilization under overload, because it fragments the large nodes less.
- **Preemption** cuts critical-pod waiting to 0 in every pattern. The cost falls on best-effort pods: more evictions, lost work, and a higher unschedulable rate (e.g. 5.1% → 6.5% under overload).

## How to run

```
make            # builds ./kubesched
make test       # unit tests
make eval       # full comparison, also writes results.csv

./kubesched run --policy binpack --preempt --timeout 300 < input.txt
./kubesched gen --pattern bursty --pods 1000 --seed 7 > workload.txt
./kubesched eval --pods 1000 --seeds 5 --timeout 300 --csv results.csv
```

## Known limitations and possible next steps

- Preemption binds the preemptor right away. kube-scheduler instead nominates the node and waits for the victims to terminate gracefully.
- Victims restart from scratch; there is no checkpointing.
- No affinity/anti-affinity, taints/tolerations or topology-spread filters yet. The plugin interface supports adding them.
- No PodDisruptionBudgets: any lower-priority pod can be evicted.
- Possible additions: per-node utilization variance (as a fragmentation metric), score-weight sweeps, and confidence intervals across seeds.
- The old tracked binaries `scheduler` and `scheduler_app` are stale (built from the previous code) and can be removed from git.

## Log

- **2026-10-26** — Replaced the single-resource service/thread scheduler with the pod/node model. Added the Filter–Score plugin framework, three placement policies, priority preemption, timeouts, the metrics suite, the workload generator, the eval harness, the CLI, unit tests and the Makefile. Switched headline utilization to the arrival window after finding the drain phase halved it under overload. All tests pass; eval results above.
