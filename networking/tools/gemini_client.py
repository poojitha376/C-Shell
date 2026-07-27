"""
Shared Gemini API helper for this project's AI tooling.

Uses Google's Gemini API (Google AI Studio) rather than OpenAI - unlike
OpenAI's API, Gemini has a genuine no-billing-required free tier (just a
Google account, no credit card), which is what these portfolio tools
actually run against. Set GEMINI_API_KEY in the environment - get one at
https://aistudio.google.com/apikey.

Note: on the free tier, Google may use prompts/outputs sent to this API for
training. Don't route anything sensitive through these tools without
enabling billing (which opts you out of that).
"""
import json
import os
import sys
import urllib.request

GEMINI_MODEL = "gemini-flash-latest"
GEMINI_EMBED_MODEL = "gemini-embedding-001"
API_BASE = "https://generativelanguage.googleapis.com/v1beta/models"


def call_gemini(messages, temperature=0, timeout=15, model=GEMINI_MODEL, quiet=False):
    """messages: list of {"role": "system"|"user"|"assistant", "content": str}.
    Returns the model's reply text, or None on any failure (no key, network,
    rate limit, malformed response) - every caller must handle None."""
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return None

    system_parts = [m["content"] for m in messages if m["role"] == "system"]
    contents = []
    for m in messages:
        if m["role"] == "system":
            continue
        role = "model" if m["role"] == "assistant" else "user"
        contents.append({"role": role, "parts": [{"text": m["content"]}]})

    body = {"contents": contents, "generationConfig": {"temperature": temperature}}
    if system_parts:
        body["systemInstruction"] = {"parts": [{"text": system_parts[0]}]}

    req = urllib.request.Request(
        f"{API_BASE}/{model}:generateContent?key={api_key}",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.load(resp)
            return data["candidates"][0]["content"]["parts"][0]["text"].strip()
    except Exception as e:  # noqa: BLE001 - deliberately broad, this is a best-effort call
        if not quiet:
            print(f"gemini_client: call failed ({e})", file=sys.stderr)
        return None


def embed_texts(texts, timeout=30, model=GEMINI_EMBED_MODEL):
    """texts: list[str]. Returns a list of embedding vectors (same order,
    one per input text), or None on failure."""
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return None

    body = {
        "requests": [
            {"model": f"models/{model}", "content": {"parts": [{"text": t}]}}
            for t in texts
        ]
    }
    req = urllib.request.Request(
        f"{API_BASE}/{model}:batchEmbedContents?key={api_key}",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.load(resp)
            return [e["values"] for e in data["embeddings"]]
    except Exception as e:  # noqa: BLE001
        print(f"gemini_client: embedding call failed ({e})", file=sys.stderr)
        return None
