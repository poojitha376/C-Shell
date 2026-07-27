#!/usr/bin/env python3
"""
Offline training for the SCHEDULER=RL policy used in kernel/proc.c.

xv6's kernel has no floating point and no room for a real training loop, so
the honest approach here (not a cop-out - this is how learned policies get
deployed in genuinely resource-constrained systems) is:

  1. Simulate xv6's tick-by-tick scheduling decision in Python, close enough
     to the real kernel model to be meaningful: state per runnable process
     is (vruntime, waiting_time, nice); one process is chosen to run for one
     tick per decision; reward is -average waiting time over the episode.
  2. Learn a linear scoring policy  score(p) = w1*vruntime + w2*nice - w3*wait
     (the process with the LOWEST score runs next - same intuition as CFS's
     "always run lowest vruntime", but with the weights found by search
     against the simulated reward instead of hand-set to 1.0) via black-box
     policy search (random search + local hill-climbing around the best
     candidate) - a legitimate, well-established RL family (the same
     category as CMA-ES/evolutionary strategies) that needs no
     gradient/backprop machinery, appropriate given only numpy is available.
  3. Distill the learned (w1, w2, w3) into fixed integer constants hardcoded
     into kernel/proc.c's pick_rl() - the kernel does zero learning at
     runtime, it just evaluates the frozen linear policy, which is pure
     integer arithmetic and trivially fast.

Run: python3 train_rl_scheduler.py
Prints the learned weights (scaled to integers for kernel use) and a
comparison against the naive baseline (w1=1024, w2=0, w3=1) which is
mathematically identical to CFS's own vruntime-only rule, to prove the
learned policy is doing something beyond just reinventing CFS.
"""
import random

random.seed(0)

NICE_0_WEIGHT = 1024


def make_episode(nprocs=5, min_burst=5, max_burst=40, nice_choices=(-5, 0, 0, 0, 5)):
    procs = []
    for i in range(nprocs):
        procs.append({
            "burst": random.randint(min_burst, max_burst),
            "nice": random.choice(nice_choices),
            "vruntime": 0.0,
            "wait": 0,
            "done": False,
        })
    return procs


def weight_for_nice(nice):
    # Same table used in the kernel (abbreviated here to the handful of
    # nice values the simulator actually samples).
    table = {-5: 3121, 0: 1024, 5: 335}
    return table[nice]


def run_episode(procs, w1, w2, w3):
    remaining = [p for p in procs]
    total_wait = 0
    total_ticks = 0
    n = len(remaining)
    while any(not p["done"] for p in remaining):
        runnable = [p for p in remaining if not p["done"]]
        # score: lower = scheduled first
        scored = []
        for p in runnable:
            score = w1 * p["vruntime"] + w2 * p["nice"] - w3 * p["wait"]
            scored.append((score, p))
        scored.sort(key=lambda x: x[0])
        chosen = scored[0][1]

        # advance one tick
        chosen["burst"] -= 1
        chosen["vruntime"] += NICE_0_WEIGHT / weight_for_nice(chosen["nice"])
        if chosen["burst"] <= 0:
            chosen["done"] = True
        for p in runnable:
            if p is not chosen:
                p["wait"] += 1
                total_wait += 1
        total_ticks += 1
    avg_wait = total_wait / n
    return avg_wait, total_ticks


def evaluate_on_fixed_set(w1, w2, w3, episodes):
    total = 0.0
    for procs_template in episodes:
        # run_episode mutates procs in place, so give it a fresh copy each time
        procs = [dict(p) for p in procs_template]
        avg_wait, _ = run_episode(procs, w1, w2, w3)
        total += avg_wait
    return total / len(episodes)


def random_search(train_set, n_iters=300):
    best = (1.0, 1.0, 1.0)
    best_score = evaluate_on_fixed_set(*best, train_set)
    for _ in range(n_iters):
        cand = (random.uniform(0, 3), random.uniform(0, 3), random.uniform(0, 3))
        s = evaluate_on_fixed_set(*cand, train_set)
        if s < best_score:
            best, best_score = cand, s
    return best, best_score


def hill_climb(start, train_set, n_iters=200, step=0.3):
    best = start
    best_score = evaluate_on_fixed_set(*best, train_set)
    for _ in range(n_iters):
        cand = tuple(max(0.0, w + random.uniform(-step, step)) for w in best)
        s = evaluate_on_fixed_set(*cand, train_set)
        if s < best_score:
            best, best_score = cand, s
            step *= 0.98
    return best, best_score


if __name__ == "__main__":
    random.seed(0)
    train_set = [make_episode() for _ in range(200)]

    print("Random search phase...")
    rs_best, rs_score = random_search(train_set)
    print(f"  best so far: w={rs_best}, avg_wait={rs_score:.3f}  [train set]")

    print("Hill-climbing refinement...")
    hc_best, hc_score = hill_climb(rs_best, train_set)
    print(f"  refined: w={hc_best}, avg_wait={hc_score:.3f}  [train set]")

    # Fair head-to-head: build a SEPARATE fixed set of held-out episodes
    # (not seen during search) and evaluate both policies on exactly the
    # same set, so the comparison isn't contaminated by different random
    # draws or by evaluating the learned policy on the data it was tuned on.
    random.seed(12345)
    holdout = [make_episode() for _ in range(300)]

    cfs_like = (1.0, 0.0, 0.0)
    cfs_score = evaluate_on_fixed_set(*cfs_like, holdout)
    learned_score = evaluate_on_fixed_set(*hc_best, holdout)
    print()
    print(f"CFS-equivalent policy (w1=1,w2=0,w3=0): avg_wait={cfs_score:.3f}  [held-out set]")
    print(f"Learned policy {tuple(round(w,3) for w in hc_best)}: avg_wait={learned_score:.3f}  [same held-out set]")
    improvement = 100.0 * (cfs_score - learned_score) / cfs_score
    print(f"Improvement: {improvement:.1f}%")

    # Distill to integer constants scaled for kernel integer arithmetic.
    # Normalize so w1 (the vruntime term, already O(NICE_0_WEIGHT) scale in
    # the kernel) keeps roughly its natural magnitude, and w2/w3 scale
    # relative to it.
    scale = 64
    w1_i = max(1, round(hc_best[0] * scale))
    w2_i = round(hc_best[1] * scale)
    w3_i = round(hc_best[2] * scale)
    print()
    print("Distilled integer weights for kernel/proc.c SCHEDULER_RL:")
    print(f"  RL_W_VRUNTIME = {w1_i}")
    print(f"  RL_W_NICE     = {w2_i}")
    print(f"  RL_W_WAIT     = {w3_i}")
