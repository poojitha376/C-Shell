#!/usr/bin/env python3
"""
LLM chat participant over the real, hand-rolled SHAM reliable-UDP transport.

Spawns the ACTUAL compiled `./client <ip> <port> --chat` binary as a
subprocess and bridges its stdin/stdout to OpenAI: lines the peer sends
arrive on the client's stdout ("peer: ..."), get forwarded to the model,
and the model's reply gets written back to the client's stdin, which the
transport then sends for real. This deliberately does NOT reimplement the
SHAM wire protocol in Python - the whole point is proving the hand-rolled
C transport can carry a real application's traffic unmodified, not
building a second implementation of the same protocol.

Usage:
    python3 llm_chat_bot.py <server_ip> <server_port> [loss_rate]

Requires the `client` binary (from `make all` in networking/) to exist
alongside this script's networking/ directory, and GEMINI_API_KEY set (see
gemini_client.py - free tier, no billing required).
"""
import os
import subprocess
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gemini_client import call_gemini  # noqa: E402

SYSTEM_PROMPT = (
    "You are chatting over a custom hand-rolled reliable-UDP transport protocol "
    "(SHAM) as a demo of the transport carrying real application traffic. Keep "
    "replies short (1-2 sentences), conversational, and don't mention you're an AI "
    "unless asked directly."
)


def ask_llm(history):
    return call_gemini([{"role": "system", "content": SYSTEM_PROMPT}] + history, temperature=0.7)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <server_ip> <server_port> [loss_rate]", file=sys.stderr)
        sys.exit(1)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    client_path = os.path.join(script_dir, "..", "client")
    if not os.path.exists(client_path):
        print(f"llm_chat_bot: {client_path} not found - run `make all` in networking/ first",
              file=sys.stderr)
        sys.exit(1)

    cmd = [client_path, sys.argv[1], sys.argv[2], "--chat"] + sys.argv[3:]
    proc = subprocess.Popen(
        cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1,
    )

    history = []
    print(f"llm_chat_bot: bridging OpenAI to {' '.join(cmd)}")

    def reader():
        for line in proc.stdout:
            line = line.rstrip("\n")
            if not line:
                continue
            print(f"[transport] {line}")
            if line.startswith("peer: "):
                msg = line[len("peer: "):]
                history.append({"role": "user", "content": msg})
                reply = ask_llm(history)
                if reply is None:
                    print("llm_chat_bot: no reply generated (check GEMINI_API_KEY)")
                    continue
                history.append({"role": "assistant", "content": reply})
                print(f"[llm] {reply}")
                try:
                    proc.stdin.write(reply + "\n")
                    proc.stdin.flush()
                except BrokenPipeError:
                    break

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    try:
        proc.wait()
    except KeyboardInterrupt:
        try:
            proc.stdin.write("/quit\n")
            proc.stdin.flush()
        except Exception:
            pass
        proc.wait()


if __name__ == "__main__":
    main()
