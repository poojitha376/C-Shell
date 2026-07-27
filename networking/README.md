# S.H.A.M. — a reliable transport built on raw UDP

A from-scratch reliable-delivery protocol (3-way/4-way handshakes, sliding window with cumulative
ACKs, adaptive RTO + fast retransmit, flow control, real-time chat) plus AI tooling layered on top.

## Build & run

```
make all
./server <port> [--chat] [loss_rate]
./client <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]   # file transfer
./client <server_ip> <server_port> --chat [loss_rate]                            # chat mode
```

Set `RUDP_LOG=1` to write a structured, timestamped protocol log to `server_log.txt` /
`client_log.txt` (see below for the exact event format).

## Design notes / what actually made this correct

- **Cumulative ACK vs. per-packet RTO** — on any ACK, the sender sweeps its *entire* send window and
  clears every slot fully covered by the new `ack_num`, not just the most recently retransmitted one.
  This matches the spec's own worked example (a lost packet gets retransmitted alone; the next
  cumulative ACK covers it plus everything sent after it, with no redundant re-sends).
- **Adaptive RTO (Jacobson/Karn, RFC 6298)** — replaces the fixed 500ms timeout. The first RTT sample
  bootstraps `srtt`/`rttvar` directly rather than being blended against a generic seed (blending a
  microsecond-scale localhost RTT into a 500ms seed via the normal EWMA formula transiently spikes the
  computed RTO *above* 500ms on the very first update — RFC 6298 special-cases the first sample for
  exactly this reason).
- **Fast retransmit** — 3 duplicate ACKs for the same `ack_num` trigger an immediate retransmit of the
  base-of-window packet, instead of waiting for its timer. Without this, a single lost packet stalls
  the entire in-flight window until each of the other (already-delivered, just not yet cumulatively
  acked) packets independently times out too — measured ~3x more retransmissions without fast
  retransmit than with it, for the same underlying loss rate.
- **Loss simulation lives in `recv_packet`**, applied only to incoming *data* packets (never
  SYN/ACK/FIN) — applying it to control packets makes handshakes/teardown flake unpredictably for no
  protocol-relevant reason.
- **Simultaneous close** — `four_way_handshake()` has one signature used identically by both sides
  (no initiator/responder distinction), so it's implemented as symmetric and simultaneous-close-safe:
  send our own FIN (retry on timeout), separately wait for the peer's FIN, ACK it when seen.
- **A real bug in the given `server.c`**: a local `int chat_mode` variable shadowed the `chat_mode()`
  function from `sham.h` in the same scope — fixed by renaming the variable. `server.c` also had a
  stdout banner printed unconditionally, which violates the spec's "file-transfer mode stdout contains
  *only* the `MD5:` line" requirement — removed.
- **A real bug in the given `client.c`**: the `--chat` branch called `chat_mode()` directly with no
  handshake at all. Fixed by calling `three_way_handshake()` first, matching every other code path.
- **A subtler bug found during testing**: the server's connection-detection loop originally *consumed*
  incoming datagrams to check for a new SYN, which meant `three_way_handshake()`'s own receive (a few
  lines later) found nothing waiting and misidentified itself as the initiator. Fixed with a
  non-consuming `MSG_PEEK` check, and a follow-up fix to also `MSG_PEEK`-filter out stray non-SYN
  packets (e.g. a duplicate retransmitted FIN from an already-closed session), which were otherwise
  making the server spuriously "initiate" a bogus connection to an old client address for ~5 seconds
  and create an empty `received_file_*` artifact.

## Verified behavior (see git history / development log for full detail)

- Byte-perfect file transfer (`md5sum` match) at 0%, 5%, 15%, and 25% simulated loss, from small files
  up to 500KB.
- Real cumulative-ACK-after-loss recovery observed directly in the log (`DROP DATA` → `TIMEOUT` →
  `RETX DATA` → a single cumulative ACK covering everything sent since).
- Flow control (`FLOW WIN UPDATE=<n>` lines) confirmed firing as the receive buffer fills/drains.
- Chat mode: real two-message exchange with mid-conversation packet loss and successful recovery,
  clean `/quit`-initiated teardown, and correct handling of a peer-initiated FIN arriving while chat
  is idle.

## AI tooling (`tools/`)

- **`log_watcher.py`** — tails a `RUDP_LOG=1` log file, computes a rolling loss rate over a sliding
  time window, and alerts when it crosses a threshold (`--diagnose` adds an optional one-line
  OpenAI-generated plain-English summary on top of the deterministic alert, which fires regardless of
  whether the LLM is reachable). Verified against real lossy transfers: correctly reports ~26-27% at a
  configured 25% loss rate.
- **`llm_chat_bot.py`** — spawns the *actual compiled* `./client --chat` binary as a subprocess and
  bridges its stdin/stdout to OpenAI, so an LLM can hold a conversation over this hand-rolled transport
  without the transport itself being reimplemented in Python.

Both require `GEMINI_API_KEY` in the environment for their LLM-dependent behavior (get a free key,
no billing required, at https://aistudio.google.com/apikey — see `tools/gemini_client.py`);
`log_watcher.py`'s core alerting is fully local/offline otherwise.
