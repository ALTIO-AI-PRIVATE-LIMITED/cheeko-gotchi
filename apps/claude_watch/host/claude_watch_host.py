#!/usr/bin/env python3
"""Claude Watch host reporter.

Runs on the machine where Claude Code runs. Tails the local transcript files
(~/.claude/projects/**/*.jsonl), aggregates token usage, reads the plan tier
from ~/.claude.json, and pushes a compact status line to an ntfy.sh topic that
the Cheeko Gotchi `claude_watch` app polls.

Stdlib only. Start it and leave it running:

    python apps/claude_watch/host/claude_watch_host.py

Privacy note: only token counts / tier labels are pushed - never any
conversation content. ntfy topics are public-by-obscurity: pick your own
random topic name and set the same name in src/app.cc.
"""

import glob
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ---- Configuration ---------------------------------------------------------

# Primary transport: serve the stats over the LAN; the device polls
# http://<this machine>:SERVE_PORT/stats directly (set the same address in
# src/app.cc kStatsUrl). No third party, no quotas. Allow the Windows
# Firewall prompt (private networks) on first run.
SERVE_PORT = 8787

# Optional remote transport: set a topic name to ALSO push via ntfy.sh when
# the device isn't on your LAN. Mind ntfy.sh's DAILY per-IP publish quota —
# continuous telemetry can exhaust it. None disables pushing.
NTFY_TOPIC = None

# ntfy.sh rate-limits per visitor IP (and NAT means the laptop and the device
# often count as ONE visitor), so be frugal: re-scan often, but only push when
# the payload actually changed, with a slow heartbeat so the device's
# staleness detector stays fed. On 429, back off exponentially — hammering
# through it keeps the rate-limit bucket empty forever.
CHECK_INTERVAL_S = 15         # how often to re-scan the transcripts
PUSH_INTERVAL_S = 60          # heartbeat push even when nothing changed
BLOCK_HOURS = 5               # Claude's rolling usage-window length

# Context window for the percent gauge. None = auto-calibrate: assume at
# least 200k and grow to the largest context actually observed (rounded up
# to 50k), since the true per-model limit isn't recorded locally.
CONTEXT_WINDOW_TOKENS = None

# Anthropic does not expose your official quota locally. If you want the
# budget gauge, set a rough tokens-per-window figure for your plan here;
# leave as None to hide the budget percent and show window progress only.
BLOCK_TOKEN_BUDGET = None

CLAUDE_DIR = os.path.join(os.path.expanduser("~"), ".claude")
CLAUDE_CONFIG = os.path.join(os.path.expanduser("~"), ".claude.json")

# ---- Transcript tailing ----------------------------------------------------

_offsets = {}   # path -> byte offset consumed
_buffers = {}   # path -> trailing partial line
_messages = {}  # message id -> (ts_epoch, in, out, cache_w, cache_r)
_context = (0.0, 0, "?")  # (ts_epoch, context_tokens, model) of newest message
_context_peak = 0  # largest context ever observed, for auto-calibration


def _iso_to_epoch(ts):
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp()
    except (ValueError, AttributeError):
        return None


def _ingest_line(line):
    global _context
    try:
        entry = json.loads(line)
    except json.JSONDecodeError:
        return
    message = entry.get("message")
    if not isinstance(message, dict):
        return
    usage = message.get("usage")
    if not isinstance(usage, dict):
        return
    ts = _iso_to_epoch(entry.get("timestamp", ""))
    if ts is None:
        return
    tok_in = usage.get("input_tokens", 0) or 0
    tok_out = usage.get("output_tokens", 0) or 0
    cache_w = usage.get("cache_creation_input_tokens", 0) or 0
    cache_r = usage.get("cache_read_input_tokens", 0) or 0
    # Streaming rewrites the same message id with growing totals; keep latest.
    msg_id = message.get("id") or entry.get("uuid")
    if msg_id:
        _messages[msg_id] = (ts, tok_in, tok_out, cache_w, cache_r)
    context_tokens = tok_in + cache_w + cache_r
    global _context_peak
    if context_tokens > _context_peak:
        _context_peak = context_tokens
    if ts >= _context[0] and context_tokens > 0:
        _context = (ts, context_tokens, str(message.get("model", "?")))


def scan_transcripts():
    for path in glob.glob(os.path.join(CLAUDE_DIR, "projects", "*", "*.jsonl")):
        try:
            size = os.path.getsize(path)
            offset = _offsets.get(path, 0)
            if size < offset:  # rotated/truncated: start over
                offset = 0
                _buffers[path] = ""
            if size == offset:
                continue
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                f.seek(offset)
                chunk = f.read()
                _offsets[path] = f.tell()
            data = _buffers.get(path, "") + chunk
            lines = data.split("\n")
            _buffers[path] = lines.pop()  # last piece may be mid-write
            for line in lines:
                if line.strip():
                    _ingest_line(line)
        except OSError:
            continue


def prune_old(now):
    cutoff = now - 8 * 86400
    for msg_id in [k for k, v in _messages.items() if v[0] < cutoff]:
        del _messages[msg_id]


# ---- Aggregation -----------------------------------------------------------

def _total(record):
    return record[1] + record[2] + record[3] + record[4]


def current_block(now):
    """Infer the active rolling window, ccusage-style: a block starts at the
    UTC-hour floor of the first message after a >= BLOCK_HOURS gap and lasts
    BLOCK_HOURS. Returns (tokens, elapsed_pct, reset_min); reset_min is -1
    when no window is active."""
    horizon = now - 2 * BLOCK_HOURS * 3600
    events = sorted(v for v in _messages.values() if v[0] >= horizon)
    block_start = None
    block_end = 0
    last_ts = None
    for event in events:
        ts = event[0]
        if block_start is None or ts >= block_end or (
                last_ts is not None and ts - last_ts >= BLOCK_HOURS * 3600):
            utc = datetime.fromtimestamp(ts, tz=timezone.utc)
            block_start = utc.replace(minute=0, second=0, microsecond=0).timestamp()
            block_end = block_start + BLOCK_HOURS * 3600
        last_ts = ts
    if block_start is None or now >= block_end:
        return 0, 0, -1
    tokens = sum(_total(e) for e in events if e[0] >= block_start)
    elapsed_pct = int((now - block_start) * 100 / (BLOCK_HOURS * 3600))
    reset_min = int((block_end - now) / 60)
    return tokens, min(elapsed_pct, 100), max(reset_min, 0)


def totals_since(cutoff):
    tin = tout = tcw = tcr = 0
    for record in _messages.values():
        if record[0] >= cutoff:
            tin += record[1]
            tout += record[2]
            tcw += record[3]
            tcr += record[4]
    return tin, tout, tcw, tcr


def read_plan():
    plan, billing = "UNKNOWN", "UNKNOWN"
    try:
        with open(CLAUDE_CONFIG, "r", encoding="utf-8") as f:
            account = json.load(f).get("oauthAccount") or {}
        # These fields can be present-but-null, so coerce None to "".
        tier = str(account.get("userRateLimitTier") or "") or \
            str(account.get("organizationRateLimitTier") or "") or \
            str(account.get("organizationType") or "")
        if tier:
            plan = tier.replace("default_", "").replace("claude_", "")
        billing_raw = str(account.get("billingType") or "")
        if billing_raw:
            billing = billing_raw.replace("stripe_", "")
    except (OSError, json.JSONDecodeError):
        pass
    return _label(plan), _label(billing)


def context_window():
    if CONTEXT_WINDOW_TOKENS:
        return CONTEXT_WINDOW_TOKENS
    calibrated = ((_context_peak + 49_999) // 50_000) * 50_000
    return max(200_000, calibrated)


def _label(text):
    """Uppercase and keep the payload delimiter-safe."""
    cleaned = "".join(
        c if c.isalnum() or c in " -." else " " for c in text.upper().replace("_", " "))
    return " ".join(cleaned.split())[:16] or "UNKNOWN"


# ---- Payload / push --------------------------------------------------------

def build_payload(now, plan, billing):
    block_tokens, elapsed_pct, reset_min = current_block(now)
    budget_pct = -1
    if BLOCK_TOKEN_BUDGET:
        budget_pct = int(block_tokens * 100 / BLOCK_TOKEN_BUDGET)
    local_midnight = datetime.now().replace(
        hour=0, minute=0, second=0, microsecond=0).timestamp()
    tin, tout, tcw, tcr = totals_since(local_midnight)
    week = sum(totals_since(now - 7 * 86400))
    ctx_tokens, model = _context[1], _context[2]
    fields = [
        "CW1",
        plan,
        str(elapsed_pct),
        str(budget_pct),
        str(block_tokens // 1000),
        str(reset_min),
        str(ctx_tokens // 1000),
        str(int(ctx_tokens * 100 / context_window())),
        _label(model.replace("claude-", "")),
        str(tin // 1000),
        str(tout // 1000),
        str(tcw // 1000),
        str(tcr // 1000),
        str(week // 1000),
        billing,
    ]
    return "|".join(fields)


def push(payload):
    request = urllib.request.Request(
        f"https://ntfy.sh/{NTFY_TOPIC}", data=payload.encode(), method="POST")
    with urllib.request.urlopen(request, timeout=10):
        pass


# ---- LAN server -------------------------------------------------------------

_current_payload = "CW1|STARTING|0|-1|0|-1|0|0|?|0|0|0|0|0|STARTING"


class StatsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/stats"):
            body = _current_payload.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):  # keep the console for payload lines
        pass


def start_lan_server():
    server = ThreadingHTTPServer(("", SERVE_PORT), StatsHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()


def main():
    global _current_payload
    start_lan_server()
    print(f"claude-watch host: serving http://<this machine>:{SERVE_PORT}/stats "
          f"on the LAN (Ctrl+C to stop)")
    if NTFY_TOPIC:
        print(f"also pushing to ntfy.sh/{NTFY_TOPIC} on change "
              f"(heartbeat {PUSH_INTERVAL_S}s)")
    plan, billing = read_plan()
    plan_refreshed = time.time()
    last_payload = None
    last_push = 0.0
    backoff_s = 0
    push_allowed_at = 0.0
    while True:
        now = time.time()
        try:
            scan_transcripts()
            prune_old(now)
            if now - plan_refreshed > 600:
                plan, billing = read_plan()
                plan_refreshed = now
            payload = build_payload(now, plan, billing)
            _current_payload = payload
            changed = payload != last_payload
            if changed:
                print(time.strftime("%H:%M:%S"), payload)
            if NTFY_TOPIC and now >= push_allowed_at and (
                    changed or now - last_push >= PUSH_INTERVAL_S):
                push(payload)
                last_push = now
                backoff_s = 0
            if changed:
                last_payload = payload
        except KeyboardInterrupt:
            raise
        except urllib.error.HTTPError as error:
            if error.code == 429:
                backoff_s = min(max(backoff_s * 2, 60), 600)
                push_allowed_at = now + backoff_s
                print(time.strftime("%H:%M:%S"),
                      f"ntfy rate limit hit; backing off {backoff_s}s",
                      file=sys.stderr)
            else:
                print(time.strftime("%H:%M:%S"), "error:", error, file=sys.stderr)
        except Exception as error:  # keep reporting through transient failures
            print(time.strftime("%H:%M:%S"), "error:", error, file=sys.stderr)
        try:
            time.sleep(CHECK_INTERVAL_S)
        except KeyboardInterrupt:
            print("stopped")
            return


if __name__ == "__main__":
    main()
