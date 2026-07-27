# C-Shell

A custom POSIX-style Unix shell, a reliable transport protocol built from scratch on raw UDP, and five
xv6 kernel scheduler policies — plus a layer of AI features built on top of each, each one hooked into
real infrastructure rather than bolted on separately.

## Layout

```
shell/        Custom shell: parser, job control, signals, pipes/redirection, built-ins
networking/   S.H.A.M. — hand-rolled reliable UDP (handshakes, sliding window, adaptive RTO, chat)
xv6/          FCFS / CFS / MLFQ / RL scheduler policies on mit-pdos/xv6-riscv, + getreadcount()
```

Each subdirectory has its own README with build instructions and implementation notes.

## AI features

**Shell**
- `? <natural language>` — an agent translates the request into a candidate command line, then
  **validates it against the shell's own real grammar parser** (`validate_syntax_internal()`) before
  ever executing it, retrying once on rejection rather than trusting the model's syntax blindly.
  Requests about background/stopped jobs ("bring back the compile job") are handled by direct
  fuzzy-matching against the job list — no LLM call needed for that path, since plain string matching
  is both cheaper and more reliable than routing a job lookup through a model.
- `predict` — a local, offline bigram model over command history (no network/LLM call at all)
  suggests the most likely next command based on what's followed the current one before.

**Networking**
- Adaptive RTO (Jacobson/Karn/RFC 6298) replacing the fixed 500ms timeout, plus fast retransmit on
  triple-duplicate-ACK — both are core parts of `sham.c` now, not an add-on layer.
- `tools/log_watcher.py` — real-time loss-rate anomaly detection over the protocol's own structured
  log, with an optional LLM-generated plain-English diagnosis on top of the deterministic alert.
- `tools/llm_chat_bot.py` — an LLM chat participant that runs *through* the real compiled `client`
  binary via subprocess, proving the hand-rolled transport can carry real application traffic without
  reimplementing the protocol in Python.

**xv6**
- A 5th scheduler policy (`SCHEDULER=RL`) whose weights were trained offline via black-box policy
  search against a Python simulation of xv6's scheduling decision, then distilled into fixed integer
  constants the kernel evaluates at zero runtime cost — see `xv6/tools/train_rl_scheduler.py` and
  `xv6/report.md` for the full methodology and an honest account of what the learned policy actually
  converged to (spoiler: not a free lunch — see the report).
- Predicted-CPU-burst-length estimation (exponential average) feeding into MLFQ's scheduling
  decisions, alongside its normal aging/demotion logic.

## A note on completeness

This repo's `shell/` was already complete when this pass started. `networking/` and `xv6/` were not —
`sham.h` declared 9 functions with zero implementations anywhere (wouldn't even link), and
`xv6/xv6_modifications.patch` was a 0-byte file with a `report.md` describing specific benchmark
numbers that had no code anywhere backing them. Both are now real, tested, and documented — see each
subdirectory's README for exactly what was broken and how it was found.

## AI provider

All LLM-backed features use **Google's Gemini API** (`shell/src/ai_agent.c`,
`networking/tools/gemini_client.py`, and the Docs++ repo's equivalent module) rather than OpenAI's -
Gemini has a genuine no-billing-required free tier (just a Google account, get a key at
https://aistudio.google.com/apikey), unlike OpenAI's API. Set `GEMINI_API_KEY` in the environment.
Note the free tier has fairly tight rate limits (rough tens of requests/minute) - every LLM call site
in this repo has a tested, graceful fallback for when a call fails or gets rate-limited (never a
crash, never a silent wrong answer).

## Two real shell bugs found and fixed during this pass

Both were pre-existing, in code that was already "complete" before this pass - found because getting
the `?` NL agent working end-to-end for the first time (previously it always failed on an invalid API
key, so its generated commands were never actually executed) immediately exercised code paths that
had apparently never been tested with real command execution:

1. **`parser.c` never NULL-terminated an atomic command's `args` array.** The loop that collects
   arguments allocates exactly `argc` slots (one per real argument, filled on the same `realloc`),
   leaving no room for a trailing `NULL`. Every builtin that iterates args expecting NULL-termination
   (`hop`, `reveal`, ...) was reading one slot past the heap allocation - confirmed with
   AddressSanitizer as a heap-buffer-overflow in `reveal()`. This explained two previously-observed
   crashes at once: `reveal -l` crashing immediately (first thing the NL agent generated for "list
   files"), and an intermittent crash after repeated `hop ..` calls (same root cause, just landing on
   adjacent heap memory that wasn't always non-zero, so it didn't crash every time). Fixed with one
   explicit NULL-terminating `realloc` after the argument-collection loop.
2. **`ai_agent.c` deleted its own curl config/payload temp files before curl could read them.**
   `popen()` only *starts* the shell pipeline asynchronously - it doesn't wait for the child to open
   the files referenced in the command. The original code called `unlink()` immediately after
   `popen()` returned, racing the spawned `curl` process; `unlink` usually won, so curl failed with
   "cannot read config from ..." before it ever made a request. This had been masked since the code
   was first written, because the *other* problem at the time (a placeholder/invalid API key) produced
   the same visible symptom (a failed call), so the race was never actually the thing under test until
   a real key made everything else work. Fixed by moving the cleanup to after `pclose()`, once the
   whole pipeline has genuinely finished with the files.
