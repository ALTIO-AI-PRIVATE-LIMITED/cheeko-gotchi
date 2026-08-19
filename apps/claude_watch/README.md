# Claude Watch

The device that monitors Claude. Your laptop tails Claude Code's local
transcript files and pushes token/plan stats to the Cheeko Gotchi, which
renders them as a live desk dashboard: how many tokens you've burned this
5-hour window, when it resets, how full the current session's context is,
today's and this week's totals, and your plan tier.

```
laptop: claude_watch_host.py  →  http://<laptop>:8787/stats  →  cheeko polls (~10s)
        (tails ~/.claude/projects/**/*.jsonl,          (same LAN)
         plan tier from ~/.claude.json)
```

Both machines must be on the same network (the device's radio is 2.4 GHz
only). Set your laptop's LAN IP in `kStatsUrl` (src/app.cc), and allow the
Windows Firewall prompt (private networks) the first time the host runs. For
remote use, the host can also push to an ntfy.sh topic (`NTFY_TOPIC` in the
host script) — but mind ntfy.sh's **daily** per-IP publish quota; continuous
telemetry can exhaust it, which is why LAN is the default.

Only token **counts** and tier labels leave your machine — never any
conversation content.

## Screens (tap to cycle)

| Screen | Shows |
| --- | --- |
| **Window** | Plan badge, ring gauge of the rolling 5-hour window, tokens burned this window, `RESETS H:MM` countdown, today's total |
| **Context** | Ring + big percent of the current session's context (green → amber → red), raw token count, model |
| **Tokens** | Today: input / output / cache-write / cache-read; 7-day total |
| **About** | Plan, billing, model, Wi-Fi and feed health |

A `LIVE` / `OFF` chip sits on every screen — `OFF` means no fresh payload for
45 s (host not running, firewall blocking, or no Wi-Fi). The device chirps
once when context (or your configured budget) crosses 90%. Auto-rotates
upright / upside-down.

## Wi-Fi setup — on the device itself

If the device isn't on Wi-Fi yet: tap the waiting screen (or the **WIFI
SETUP** button on the About screen). It scans and lists nearby networks —
the radio is 2.4 GHz-only, so everything shown is joinable — with signal
bars and a padlock for secured ones. Tap your network, type the password on
the on-screen keyboard (page key cycles abc / ABC / 123 symbols), hit **OK**,
and it joins and saves the credentials to flash (they survive reboots and
re-flashes). Serial provisioning (`WIFI <ssid>|<pass>` at 115200 baud) still
works too.

## Setup

1. Pick your own random topic name and set it in **both**
   [host/claude_watch_host.py](host/claude_watch_host.py) (`NTFY_TOPIC`) and
   [src/app.cc](src/app.cc) (`kNtfyTopic`). ntfy topics are
   public-by-obscurity: anyone who guesses the name can read the counters.
2. Start the reporter on the machine running Claude Code:

   ```bash
   python apps/claude_watch/host/claude_watch_host.py
   ```

3. Run the device app:

   ```bash
   python tools/cheeko run apps/claude_watch     # simulator
   python tools/cheeko flash apps/claude_watch   # hardware (Wi-Fi provisioned)
   ```

In the simulator there is no real cloud fetch: paste a payload line (see
below) into the cloud panel, or POST it to `/text`.

## Honest numbers

Anthropic does not expose your official remaining quota locally. The 5-hour
window and its reset countdown are **inferred from your own activity
timestamps** (transcript files), the same approach the ccusage tool uses. The
ring shows elapsed window time by default; set `BLOCK_TOKEN_BUDGET` in the
host script to gauge tokens against a rough per-window budget instead. The
context percent auto-calibrates to the largest context ever observed (at
least 200k), since the true per-model window isn't recorded locally — or pin
it with `CONTEXT_WINDOW_TOKENS`.

## Rate limits

ntfy.sh limits requests per visitor IP, and behind home NAT the laptop and
the device look like ONE visitor — hence the frugal cadence: the host pushes
only when the stats change (60 s heartbeat otherwise) and backs off
exponentially on HTTP 429; the device polls every 20 s. If you still hit
limits (e.g. several devices on one network), self-host ntfy or use an
authenticated topic.

## Payload format

Pipe-delimited, pushed on change (heartbeat every 60 s):

```
CW1|<plan>|<window elapsed %>|<budget % or -1>|<window tokens k>|<reset min or -1>|
<context k>|<context %>|<model>|<today in k>|<today out k>|<today cache-write k>|
<today cache-read k>|<7-day k>|<billing>
```

(single line; wrapped here for readability)
