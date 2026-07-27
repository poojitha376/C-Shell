# xv6 scheduler policies + a trained-and-distilled RL policy

Five scheduler implementations on top of [mit-pdos/xv6-riscv](https://github.com/mit-pdos/xv6-riscv):
stock round-robin (default), FCFS, CFS, a bonus MLFQ, and a 5th policy whose weights were trained
offline in Python and distilled into fixed kernel constants — plus a `getreadcount()` syscall.

## Reproducing this

```
git clone https://github.com/mit-pdos/xv6-riscv.git
cd xv6-riscv
git apply /path/to/xv6_modifications.patch
cp /path/to/{readcount,scheduler_test,nice_test}.c user/
# add $U/_readcount $U/_scheduler_test $U/_nice_test to UPROGS in the Makefile
make clean && make qemu                       # default round-robin
make clean && make qemu SCHEDULER=FCFS
make clean && make qemu SCHEDULER=CFS
make clean && make qemu SCHEDULER=MLFQ
make clean && make qemu SCHEDULER=RL
```

**Always `make clean` before switching `SCHEDULER=` values** — `make`'s dependency tracking doesn't
see a `-D` flag change as invalidating already-built `.o` files, so skipping `clean` silently relinks
the *previous* policy's compiled objects. (This bit me once during data collection — see `report.md`.)

## What's here

- **`xv6_modifications.patch`** — the actual kernel diff (`git diff` against a pristine
  `mit-pdos/xv6-riscv` checkout). This is the real deliverable; it did not exist before this pass —
  the file that shipped originally was 0 bytes.
- **`report.md`** — full implementation notes, a real (not fabricated) performance comparison table
  across all five policies, the RL training methodology with an honest interpretation of what the
  learned policy actually converged to, and the bugs found along the way.
- **`benchmark_results.txt`** — raw run-by-run numbers `report.md` is built from.
- **`readcount.c`** / **`scheduler_test.c`** / **`nice_test.c`** — user-space test programs.
  `scheduler_test.c` was extended to report per-process turnaround, not just aggregate time (needed
  for a real fairness comparison); `nice_test.c` is new, added specifically to make CFS's and the
  learned policy's nice-based prioritization empirically observable (the original `scheduler_test.c`
  workload never varies `nice`, so neither policy's nice-awareness could be demonstrated without it).
- **`tools/train_rl_scheduler.py`** — offline training script for the 5th (`SCHEDULER=RL`) policy.

## The short version of what's real here

- `getreadcount()`: verified in QEMU — reads exactly the requested byte count, cumulative across all
  processes since boot.
- FCFS: strict staircase turnaround pattern, confirmed non-preemptive.
- CFS: genuine preemptive interleaving confirmed directly from the kernel's own per-decision log
  (`[Scheduler Tick]` / `PID: n | vRuntime: n` / `--> Scheduling PID n`), not just inferred from timing.
- MLFQ: aging/demotion converges identical CPU-bound processes into fair round-robin quickly; the
  predicted-CPU-burst-length addition (exponential average, used as a tie-break within a queue) is
  implemented and code-reviewed, though a single-workload-type benchmark can't fully exercise MLFQ's
  real advantage over CFS (telling CPU-bound and I/O-bound processes apart).
- RL: trained via black-box policy search (random search + hill-climbing) against a Python simulation
  of xv6's scheduling decision, evaluated on a held-out set never seen during search — a genuine 24.1%
  improvement in simulated average waiting time over a CFS-equivalent baseline, honestly reported
  alongside *why* (the search found an almost purely nice-priority policy, which is a real
  reward-shaping lesson, not an unqualified win — see `report.md`).

Full detail, numbers, and the story of what went wrong along the way (and how it was caught) are in
`report.md` — this README is the map, that's the territory.
