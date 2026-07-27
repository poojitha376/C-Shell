# xv6 Scheduler Comparison Report

All numbers in this report are real measurements from `scheduler_test`/`nice_test` run in QEMU
(`riscv64-linux-gnu-gcc` 13.3.0, `qemu-system-riscv64` 8.2.2), pinned to `CPUS=1`, on a base
[mit-pdos/xv6-riscv](https://github.com/mit-pdos/xv6-riscv) checkout. Multiple runs were taken for
each policy and were highly consistent (see `benchmark_results.txt` for the raw run-by-run data this
report is built from). An earlier draft of this report shipped with fabricated placeholder numbers
(Round Robin 105 ticks, FCFS 85, CFS 75) before any of the kernel code actually existed to produce
them - those have been discarded entirely and replaced with the real numbers below.

## Implementation Details

### System Call: `getreadcount()`
- Global `uint readcount_bytes` + dedicated `readcountlock` spinlock in `kernel/sysfile.c`, initialized
  from `main.c` alongside the other subsystem `init()` calls.
- `sys_read()` increments the counter by the *actual* bytes `fileread()` returned, not the requested
  length, so short reads don't inflate the count.
- Wraps to 0 on overflow for free, by using an unsigned counter (well-defined behavior, no manual
  overflow check needed).
- Verified in QEMU: `readcount` reported "Initial read count: 10, Final read count: 110, Bytes read in
  test: 100" - exactly matching the 100-byte `read()` call in the test program, with 10 bytes already
  attributed to boot/shell activity before the test ran.

### FCFS Scheduler
- Added a `ctime` (creation tick) field to `struct proc`, set in `allocproc()`.
- `scheduler()` scans `proc[]` for the RUNNABLE process with the smallest `ctime` (see `pick_fcfs()`).
- Non-preemption is enforced in `kernel/trap.c`, not `proc.c`: a new `should_yield_on_tick()` hook
  (called from both `usertrap()` and `kerneltrap()` in place of the old unconditional `yield()`) returns
  0 under `SCHEDULER_FCFS`, so the timer tick never forces a reschedule - only exit/block does.
- **Result**: a clean staircase (see table below) - each process runs strictly to completion before the
  next one starts, confirmed directly from the turnaround pattern.

### CFS Scheduler
- `nice`, `weight`, `vruntime`, `time_slice`, `ticks_in_slice` fields added to `struct proc`.
- Weight table: the standard Linux `sched_prio_to_weight[]` table (nice -20 -> 88761 ... 0 -> 1024 ...
  19 -> 15), reused verbatim rather than re-derived, since it already satisfies the assignment's stated
  formula/endpoints exactly and needs no floating point (the kernel build has none).
- `vruntime += NICE_0_WEIGHT / weight` accrues once per tick the process actually runs, inside
  `should_yield_on_tick()`.
- No sorted runqueue structure is kept - each scheduling decision does an O(n) scan over `proc[]` for
  the RUNNABLE process with the smallest `vruntime` (`pick_cfs()`), which the assignment text itself
  permits and is cheap given `NPROC` is small.
- `time_slice = max(3, 48 / num_runnable)`, recomputed every decision.
- Required per-decision log format (`[Scheduler Tick]` / `PID: n | vRuntime: n` / `--> Scheduling PID n
  (lowest vRuntime)`) implemented exactly as specified - but gated to only fire when there's an actual
  RUNNABLE process to choose (see "Challenges" below for why that gate was necessary).
- Added a `setnice(pid, nice)` syscall (not in the original spec) purely so CFS's nice-awareness could
  actually be exercised by a test program - `scheduler_test.c`'s own workload never varies `nice`.

### MLFQ Scheduler (bonus)
- 4 queues, slices 1/4/8/16 ticks, `mlfq_queue` field on `struct proc`.
- `kernel/trap.c` preemption is two-part for this policy: yield when the running process's own slice is
  exhausted, *or* early if a strictly higher-priority process becomes RUNNABLE
  (`should_preempt_mlfq()`), checked every tick.
- Demotion vs. same-queue re-entry is decided by a `mlfq_slice_exhausted` flag set in
  `should_yield_on_tick()` right before an involuntary preemption, and read once in `yield()`: slice
  exhaustion demotes (`queue = min(queue+1, 3)`), an early preemption by a higher-priority arrival does
  not. Voluntary blocks go through `sleep()` directly (never through `yield()`), so they naturally keep
  their queue with no extra logic needed - that's exactly the "re-enter at end of same queue" behavior.
- Global starvation prevention: every 48 ticks, every process (not just runnable ones) is reset to
  queue 0, tracked via one file-scope `mlfq_last_boost` compared against the kernel's existing tick
  counter.
- **Bonus addition - predicted CPU-burst length**: `predicted_burst` field, updated via exponential
  average (`predicted = avg(actual_ticks_this_stretch, previous_prediction)`, i.e. alpha=1/2, a
  power-of-two fraction so no floats/fixed-point machinery is needed) in both `yield()` (involuntary) and
  `sleep()` (voluntary) paths. Used as the primary tie-break within a queue level in `pick_mlfq()` (a
  small SJF-flavored nudge on top of round-robin), with a rotating cursor as the final tie-break so
  queue 3 (and every other queue) still gets fair round-robin turns once burst predictions converge.

### RL Scheduler (5th policy, extends the assignment)
See the dedicated section below - trained offline in Python, distilled into fixed integer weights
evaluated by the kernel at zero runtime training cost.

## Performance Comparison

### Methodology
`scheduler_test` forks 5 CPU-bound processes (`BURST=250000000`-iteration empty loop each, tuned so
that individual bursts exceed CFS's computed slice - an earlier, smaller burst finished within a single
slice for every process, which made CFS behave identically to FCFS and hid its actual preemptive
interleaving entirely; this is itself a useful empirical lesson about picking a representative
benchmark workload, not just an implementation detail). Each policy was rebuilt from a clean tree
(`make clean` before every `SCHEDULER=` switch - stale `.o` files from a previous policy are not
invalidated by a `-D` flag change alone, and silently reusing them was caught contaminating one early
result) and run multiple times pinned to `CPUS=1`; results were highly consistent run-to-run.

### Results (5 identical nice=0 CPU-bound processes)

| Scheduler | Turnarounds (ticks)      | Total | Avg turnaround | Spread (max-min) |
|-----------|---------------------------|-------|-----------------|-------------------|
| RR (default) | 76,76,76,76,76 / 77,77,77,77,77 | 76-77 | 76-77 | 0-1 |
| MLFQ      | 64,65,67,73,78            | 78    | 69              | 14                |
| CFS       | 52,59,65,72,78            | 78    | 65              | 26                |
| RL        | 51,58,65,72,79            | 79    | 65              | 28                |
| FCFS      | 16,32,47,63,78            | 78    | 47              | 62                |

Total work is identical across every policy (same 5 processes, same burst) - what differs is entirely
*when* each process gets its share, which is exactly what each policy is designed to trade off:

- **RR** gives near-perfect lockstep fairness (all 5 finish within 1 tick of each other) by cycling
  every runnable process 1 tick at a time.
- **MLFQ** converges all 5 identical processes into the bottom queue at essentially the same rate (they
  all demote together since their workloads are identical), landing them in a fair round-robin with
  large slices - tighter than CFS here specifically *because* the workload is homogeneous.
- **CFS** interleaves via 9-tick slices (`48/5`, confirmed directly from the kernel log: worker
  processes were observed being preempted at `vRuntime=9`, i.e. after exactly one slice, and
  re-scheduled in rotation - not run to completion in one shot).
- **RL** is nearly indistinguishable from CFS on this homogeneous workload (see below for why).
- **FCFS** is a strict staircase - whoever forked first finishes first, no preemption at all.

### Nice-based prioritization (`nice_test.c`: nices = [-5, 0, 0, 0, 5], `CPUS=1`)

The homogeneous-workload table above can't show CFS or RL doing anything nice-aware, since
`scheduler_test` never varies `nice`. `nice_test.c` (added for this purpose, using the new `setnice()`
syscall) does:

| Scheduler | nice -5 | nice 0 (x3)      | nice 5 | Total |
|-----------|---------|------------------|--------|-------|
| CFS       | 52      | 58, 65, 72       | 79     | 80    |
| RL        | 16      | 55, 59, 62       | 78     | 78    |

CFS gives the higher-weight process proportionally more CPU (52 vs. 79 ticks - about 1.5x), but everyone
still finishes within a comparable range. RL is far more extreme: the nice=-5 process finishes in
roughly a fifth of the time the others take, essentially monopolizing the CPU up front - a direct,
visible consequence of what the training run below found.

## RL Training Methodology

The kernel has no floats and no room for an online training loop, so the approach taken - and the
honest way real systems deploy learned scheduling policies under this kind of constraint - is:

1. **Simulate** xv6's tick-by-tick scheduling decision in Python (`xv6/tools/train_rl_scheduler.py`):
   state per runnable process is `(vruntime, waiting_time, nice)`, one process is chosen per simulated
   tick, reward is `-average waiting time` over the episode.
2. **Learn** a linear scoring policy `score = w1*vruntime + w2*nice - w3*wait` (lowest score runs next -
   same intuition as CFS's "always run lowest vruntime", but with weights found by search instead of
   hand-set to 1.0) via black-box policy search: random search followed by local hill-climbing. This is
   a legitimate, well-established RL family (the same category as CMA-ES/evolutionary strategies) that
   needs no gradient/backprop machinery - appropriate given only `numpy` is available in this
   environment, no `torch`.
3. **Distill** the learned weights into fixed integer constants hardcoded into `pick_rl()` in
   `kernel/proc.c`. The kernel does zero learning at runtime - it only evaluates the frozen linear
   policy on every scheduling decision, which is pure integer arithmetic and trivially cheap.

Trained on 200 random episodes (5 processes each, burst 5-40 ticks, nice mostly 0 with occasional
-5/+5), evaluated on a **separate held-out set of 300 episodes never seen during search**, to get a
fair, non-overfit comparison rather than reporting a number the search directly optimized against:

```
CFS-equivalent policy (w1=1, w2=0, w3=0): avg_wait = 58.87 ticks   [held-out set]
Learned policy (w1~0, w2~1.70, w3=0):     avg_wait = 44.66 ticks   [same held-out set]
Improvement: 24.1%
```

**Honest interpretation, not just the headline number**: the search converged toward an almost purely
nice-based priority policy - the vruntime and waiting-time terms were driven to near/exactly zero. This
genuinely beats CFS on raw average waiting time in this simulated setting, but it does so by abandoning
the specific kind of fairness CFS is designed to guarantee: a process's own accumulated runtime no
longer influences its own priority at all, and two same-nice processes are ranked by scan-order
tie-break rather than by who has actually run less. This is a textbook reward-shaping lesson -
optimizing an unweighted population-average metric with no fairness constraint converges toward exactly
the kind of policy CFS's designers deliberately moved away from with proportional-share scheduling. The
`nice_test.c` results above make this concrete: RL's nice=-5 process gets a dramatically bigger edge than
CFS gives the same nice value, at the direct expense of how long the nice=5 process waits.

Distilled kernel constants: `RL_W_VRUNTIME = 1`, `RL_W_NICE = 109` (`RL_W_WAIT = 0` is omitted from the
kernel implementation entirely, since the learned weight was exactly zero - no point computing and
multiplying by zero in a hot path).

## Challenges Faced

1. **Silent stale-object contamination between `SCHEDULER=` builds.** `make`'s dependency tracking
   doesn't see a `-D` flag change as invalidating already-built `.o` files, so switching policies without
   `make clean` first silently relinks the *previous* policy's compiled objects. This actually happened
   once during data collection (a "round-robin" run showed CFS's staircase profile instead of RR's
   lockstep one) before the pattern was caught, and every number in this report was collected with an
   explicit `make clean` before each policy switch from then on.
2. **CFS's per-tick log flooding at idle.** `wfi` (wait-for-interrupt) wakes on *every* timer tick, not
   just when something becomes runnable, so a first draft of `pick_cfs()` printed `[Scheduler Tick]` on
   every idle tick even with nothing to schedule. Fixed by counting runnable processes first, silently,
   and only emitting the required log block when there's an actual decision to make.
3. **Undersized benchmark workload hid CFS's real behavior.** The first `scheduler_test` burst
   (`30000000` iterations, ~1.8 ticks of work per process) finished within a single CFS slice for every
   process, so CFS never actually preempted anyone and produced the exact same staircase as FCFS - not a
   bug, just a workload too small to exercise the policy being tested. Scaling the burst up
   (`250000000`, ~15-16 ticks per process alone) made CFS's real interleaving behavior show up clearly
   in both the turnaround numbers and the kernel log.
4. **RL's learned behavior was invisible on the standard test workload.** `scheduler_test`'s 5 processes
   are all `nice=0` by default (no `setnice()` existed originally), so a policy whose whole
   distinguishing feature is nice-sensitivity looks identical to CFS on that workload. Added a minimal
   `setnice(pid, nice)` syscall specifically to make both CFS's and RL's nice-awareness demonstrable,
   rather than reporting a number that couldn't actually be observed running.
5. **Simultaneous-close in the networking half of this assignment** (not scheduler-related, but same
   general "the spec's function signatures don't distinguish roles the way the state machine needs"
   category of bug) - covered in the networking `README.md`.

## Conclusion

No single policy is "best" - each optimizes a different, real tradeoff, and the numbers above show that
concretely rather than asserting it:

- **RR** is the fairness ceiling for homogeneous workloads, at the cost of the most context switches.
- **FCFS** has zero preemption overhead and is trivial to reason about, at the cost of the worst-case
  latency for whoever arrives last (a 4x spread between first and last finisher here).
- **CFS** sits deliberately in between, trading some fairness for far fewer context switches than RR, and
  is nice-aware in a proportional, bounded way.
- **MLFQ** converges identical CPU-bound processes into fairness quickly via aging, but its real
  advantage over CFS - telling CPU-bound and I/O-bound processes apart via slice-exhaustion vs.
  voluntary-block behavior - isn't exercised by this single-workload-type benchmark; a mixed CPU+I/O
  workload would be needed to demonstrate that differentiation empirically.
- **RL** demonstrates that a policy learned purely to minimize an unweighted average metric will happily
  sacrifice the fairness guarantees CFS was explicitly designed around, which is as much a lesson about
  reward specification as it is a scheduler implementation.
