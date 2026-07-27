#!/usr/bin/env python3
"""
Congestion/anomaly log watcher for the SHAM protocol.

Tails server_log.txt / client_log.txt (produced when the C client/server
run with RUDP_LOG=1) and computes a rolling loss/retransmit rate over a
sliding time window, printing an alert when it crosses a threshold. This is
deliberately a separate tool from the C binaries it watches - monitoring
infrastructure living outside the core system it observes is normal
practice, and it means the watcher can be pointed at a log file the
whole time a real transfer runs without adding any overhead to the
protocol implementation itself.

Usage:
    python3 log_watcher.py <path-to-log-file> [--window SECONDS] [--threshold FRACTION] [--diagnose]

--diagnose additionally calls Gemini periodically to turn the raw counters
into a one-line human-readable summary (requires GEMINI_API_KEY - see
gemini_client.py). Without it, the watcher is fully local/offline - the
deterministic threshold alert never depends on the LLM being reachable.
"""
import argparse
import collections
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gemini_client import call_gemini  # noqa: E402

LOG_LINE_RE = re.compile(
    r"^\[(?P<ts>[\d-]+ [\d:.]+)\] \[LOG\] (?P<event>.+)$"
)

EVENT_KIND_RE = re.compile(
    r"^(SND DATA|RCV DATA|SND ACK|RCV ACK|TIMEOUT|RETX DATA|DROP DATA|FAST RETX|FLOW WIN UPDATE)"
)


def classify(event_text):
    m = EVENT_KIND_RE.match(event_text)
    if not m:
        return None
    kind = m.group(1)
    if kind in ("DROP DATA",):
        return "drop"
    if kind in ("TIMEOUT", "RETX DATA", "FAST RETX"):
        return "retx"
    if kind in ("RCV DATA",):
        # Loss is simulated on the RECEIVE side (see sham.c's recv_packet),
        # so "successfully arrived" data packets are RCV DATA in *this*
        # side's own log - not SND DATA, which only appears in the sender's
        # log and would never appear here at all, making drop/(drop+sent)
        # divide by a count that's always zero on a pure receiver.
        return "received"
    return None


def diagnose_with_llm(window_counts):
    prompt = (
        "You are monitoring a custom reliable-UDP protocol's log. In the last window: "
        f"{window_counts['received']} data packets received, {window_counts['drop']} dropped, "
        f"{window_counts['retx']} retransmitted. Reply with ONE short sentence describing "
        "the current network condition (e.g. healthy / mild loss / severe congestion) and, "
        "if severe, one concrete suggestion. No preamble."
    )
    return call_gemini([{"role": "user", "content": prompt}], timeout=10)


def follow(path):
    """Yield new lines appended to `path`, like `tail -f`."""
    with open(path, "r") as f:
        f.seek(0, os.SEEK_END)
        while True:
            line = f.readline()
            if not line:
                time.sleep(0.2)
                continue
            yield line.rstrip("\n")


def main():
    # A monitoring tool that buffers its own alerts defeats the purpose when
    # piped to a file/tee instead of a live tty - force line buffering so
    # every print is visible immediately, not just on process exit.
    sys.stdout.reconfigure(line_buffering=True)

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logfile")
    ap.add_argument("--window", type=float, default=5.0, help="rolling window in seconds")
    ap.add_argument("--threshold", type=float, default=0.15,
                     help="drop/(received+drop) fraction that triggers an alert")
    ap.add_argument("--diagnose", action="store_true",
                     help="also call OpenAI for a plain-English diagnosis on each alert")
    args = ap.parse_args()

    if not os.path.exists(args.logfile):
        print(f"log_watcher: waiting for {args.logfile} to be created...", file=sys.stderr)
        while not os.path.exists(args.logfile):
            time.sleep(0.5)

    events = collections.deque()  # (timestamp, kind)
    print(f"log_watcher: watching {args.logfile} "
          f"(window={args.window}s, alert threshold={args.threshold:.0%})")

    for line in follow(args.logfile):
        m = LOG_LINE_RE.match(line)
        if not m:
            continue
        kind = classify(m.group("event"))
        if kind is None:
            continue

        now = time.time()
        events.append((now, kind))
        while events and now - events[0][0] > args.window:
            events.popleft()

        counts = {"received": 0, "drop": 0, "retx": 0}
        for _, k in events:
            counts[k] += 1

        total = counts["received"] + counts["drop"]
        rate = counts["drop"] / total if total > 0 else 0.0
        if total >= 5 and rate >= args.threshold:
            print(f"[ALERT] loss rate {rate:.0%} over last {args.window:.0f}s "
                  f"(received={counts['received']} drop={counts['drop']} retx={counts['retx']})")
            if args.diagnose:
                diagnosis = diagnose_with_llm(counts)
                if diagnosis:
                    print(f"  -> {diagnosis}")


if __name__ == "__main__":
    main()
