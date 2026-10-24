# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22"]
# ///
"""Mac stand-in for the iOS app: talks to the board over BLE exactly as docs/ble-protocol.md says.

    uv run hardware/tools/board_test.py all          # full check (see README.md)
    uv run hardware/tools/board_test.py snap --png out/snap.png
    uv run hardware/tools/board_test.py cmd "move 100 0" click
"""
import argparse
import asyncio
import json
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

BASE = "6e400001-a9e8-4b1b-9c39-6d6f8a3c"
SVC, CMD, REPLY, STATUS, VIDEO, NET = (BASE + s for s in ("0000", "0001", "0002", "0003", "0004", "0005"))
START, END, IDR, CONFIG, BENCH, SNAP = 1, 2, 4, 8, 16, 32
FFMPEG = shutil.which("ffmpeg") or "/opt/homebrew/bin/ffmpeg"


class Reassembler:
    """Rebuilds access units from video notifies; after a seq gap waits for the next CONFIG/IDR."""

    def __init__(self):
        self.expect = None
        self.cur = None  # (flags, ts, bytearray)
        self.waiting = False
        self.aus, self.gaps, self.bench_bytes, self.bytes = [], 0, 0, 0
        self.first_at = self.last_at = None

    def feed(self, data: bytes):
        now = time.monotonic()
        self.first_at = self.first_at or now
        self.last_at = now
        self.bytes += len(data)
        seq, flags = struct.unpack_from("<HB", data)
        payload = data[3:]
        if self.expect is not None and seq != self.expect:
            self.gaps += 1
            self.cur, self.waiting = None, True
        self.expect = (seq + 1) & 0xFFFF
        if flags & BENCH:
            self.bench_bytes += len(payload)
            return
        if flags & START:
            if self.waiting and not flags & (CONFIG | IDR):
                return
            self.waiting = False
            ts = struct.unpack_from("<I", payload)[0]
            self.cur = (flags, ts, bytearray(payload[4:]))
        elif self.cur is not None:
            self.cur[2].extend(payload)
        if flags & END and self.cur is not None:
            self.aus.append((self.cur[0] | flags, self.cur[1], bytes(self.cur[2])))
            self.cur = None

    def annexb(self) -> bytes:
        return b"".join(au for _, _, au in self.aus)


class Board:
    def __init__(self, client: BleakClient):
        self.c = client
        self.next_id = 1
        self.pending: dict[str, asyncio.Future] = {}
        self.status: dict = {}
        self.video = Reassembler()
        self.write_lock = asyncio.Lock()  # bleak/CoreBluetooth: one write-with-response per characteristic at a time

    async def start(self):
        await self.c.start_notify(REPLY, self._on_reply)
        await self.c.start_notify(STATUS, self._on_status)
        await self.c.start_notify(VIDEO, lambda _, d: self.video.feed(bytes(d)))

    def _on_reply(self, _, data: bytearray):
        text = data.decode(errors="replace")
        rid, _, rest = text.partition(" ")
        if rid in ("log", "app"):  # relayed app debug lines (the phone's app is the other peer)
            print(f"{time.strftime('%H:%M:%S')} [{rid}] {rest}", flush=True)
            return
        fut = self.pending.pop(rid, None)
        if fut and not fut.done():
            fut.set_result(rest)

    def _on_status(self, _, data: bytearray):
        try:
            self.status = json.loads(data)
        except ValueError:
            print("bad status json:", data)

    async def cmd(self, line: str, timeout: float = 15) -> tuple[str, float]:
        rid = str(900000 + self.next_id)  # the app shares this link and uses ids 100000-800000
        self.next_id += 1
        fut = asyncio.get_running_loop().create_future()
        self.pending[rid] = fut
        t0 = time.monotonic()
        async with self.write_lock:
            await self.c.write_gatt_char(CMD, f"{rid} {line}".encode(), response=True)
        reply = await asyncio.wait_for(fut, timeout)
        return reply, (time.monotonic() - t0) * 1000

    async def net(self) -> dict:
        net = json.loads(await self.c.read_gatt_char(NET))  # first read triggers macOS/iOS pairing
        return {**net, "pass": "*" * len(net.get("pass", ""))}  # never print Wi-Fi passwords


async def find_board(timeout: float):
    print("scanning for 'ESP32 Agent ...'")
    dev = await BleakScanner.find_device_by_filter(lambda d, ad: (d.name or ad.local_name or "").startswith(("ESP32 Agent", "iPhone Agent")),
                                                   timeout=timeout)
    if not dev:
        sys.exit("board not found (is it powered and advertising?)")
    print("found", dev.name, dev.address)
    return dev


def save_video(board: Board, h264: Path, png: Path | None):
    h264.parent.mkdir(parents=True, exist_ok=True)
    h264.write_bytes(board.video.annexb())
    print(f"wrote {h264} ({h264.stat().st_size} bytes, {len(board.video.aus)} access units, {board.video.gaps} gaps)")
    if png:
        r = subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", str(h264), "-update", "1", str(png)],
                           capture_output=True, text=True)
        print(f"decoded last frame -> {png}" if r.returncode == 0 and png.exists() else f"ffmpeg failed: {r.stderr.strip()}")


async def t_status(b: Board):
    await asyncio.sleep(2.5)
    print("status:", json.dumps(b.status))


async def t_bench(b: Board, kb: int):
    b.video = Reassembler()
    reply, ms = await b.cmd(f"bench {kb}", timeout=120)
    host_s = (b.video.last_at - b.video.first_at) if b.video.first_at else 0
    kbps = b.video.bench_bytes * 8 / 1000 / host_s if host_s else 0
    print(f"bench {kb} KB: reply '{reply}' after {ms:.0f} ms; received {b.video.bench_bytes} B payload, "
          f"{kbps:.0f} kbps at the Mac, {b.video.gaps} gaps")


async def t_snap(b: Board, h264: Path, png: Path | None):
    b.video = Reassembler()
    reply, ms = await b.cmd("snap", timeout=60)
    print(f"snap: '{reply}' in {ms:.0f} ms, {b.video.bytes} bytes over BLE")
    if reply == "ok":
        save_video(b, h264, png)


async def t_stream(b: Board, seconds: float, h264: Path, png: Path | None):
    b.video = Reassembler()
    print("stream on:", (await b.cmd("stream on"))[0])
    await asyncio.sleep(seconds)
    print("stream off:", (await b.cmd("stream off"))[0])
    await asyncio.sleep(0.5)
    v = b.video
    dur = (v.last_at - v.first_at) if v.first_at else 0
    print(f"stream {seconds:.0f}s: {len(v.aus)} AUs, {v.bytes * 8 / 1000 / dur if dur else 0:.0f} kbps over BLE, {v.gaps} gaps")
    save_video(b, h264, png)


async def t_hid(b: Board):
    for line in ["ping", "home", "move 200 150", "click", "scroll -3 0", "key esc"]:
        reply, ms = await b.cmd(line)
        print(f"{line:>14} -> {reply} ({ms:.0f} ms)")


async def t_stress(b: Board, seconds: float, load: str, kb: int, every: float):
    """Video load (`stream on`, or a synthetic `bench KB`) while ping/move goes out every `every` s;
    prints reply latency p50/p95/max and missing replies (no reply within 5 s)."""
    await asyncio.sleep(1.5)  # first status
    up0 = b.status.get("up", 0)
    b.video = Reassembler()
    bench = None
    if load == "stream":
        print("stream on:", (await b.cmd("stream on"))[0])
    else:
        bench = asyncio.create_task(b.cmd(f"bench {kb}", timeout=seconds + 300))
    lat, missing, tasks = [], [], []

    async def one(line: str):
        try:
            lat.append((await asyncio.wait_for(b.cmd(line, timeout=5), 6))[1])  # also bounds the GATT write
        except asyncio.TimeoutError:
            missing.append(line)

    lines, end = ["ping", "move 1 0", "ping", "move -1 0"], time.monotonic() + seconds
    while time.monotonic() < end:
        tasks.append(asyncio.create_task(one(lines[len(tasks) % len(lines)])))
        await asyncio.sleep(every)
    await asyncio.gather(*tasks)
    print(f"sent {len(tasks)} commands", flush=True)
    if bench:
        try:
            print("bench:", (await bench)[0], flush=True)
        except asyncio.TimeoutError:
            print("bench: no reply")
    else:
        print("stream off:", (await b.cmd("stream off"))[0])
    await asyncio.sleep(1.5)
    lat.sort()
    pct = lambda q: lat[min(len(lat) - 1, int(q * len(lat)))] if lat else 0  # noqa: E731
    v = b.video
    dur = (v.last_at - v.first_at) if v.first_at else 0
    print(f"stress {seconds:.0f}s ({load}): {len(tasks)} commands, {len(missing)} missing, latency ms "
          f"p50 {pct(0.5):.0f} p95 {pct(0.95):.0f} max {pct(1):.0f}; video {v.bytes * 8 / 1000 / dur if dur else 0:.0f} "
          f"kbps, {v.gaps} gaps")
    st = b.status
    reboot = st.get("up", 0) < up0
    print(f"board: rst {st.get('rst')} up {st.get('up')} ms{' (REBOOTED)' if reboot else ''}, ble {st.get('ble')}")


async def t_app(b: Board, lines: list[str], seconds: float, snap_every: float, out: Path):
    for line in lines:
        for attempt in range(3):  # the reply can be lost while the phone's app saturates the link
            try:
                reply, ms = await b.cmd(f"app {line}", timeout=5)
                print(f"app {line} -> {reply} ({ms:.0f} ms)", flush=True)
                break
            except TimeoutError:
                print(f"app {line}: no reply (try {attempt + 1})", flush=True)
    end, n = time.monotonic() + seconds, 0
    while time.monotonic() < end:
        if snap_every:
            n += 1
            try:
                await t_snap(b, out / "live.h264", out / f"live-{n}.png")
            except Exception as e:  # keep watching the log even if a snap fails
                print("snap:", e, flush=True)
            await asyncio.sleep(snap_every)
        else:
            await asyncio.sleep(1)


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("test", choices=["all", "info", "bench", "snap", "stream", "hid", "cmd", "app", "stress"])
    ap.add_argument("lines", nargs="*", help="command lines for `cmd`")
    ap.add_argument("--kb", type=int, default=256)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--out", type=Path, default=Path("hardware/tools/out"))
    ap.add_argument("--png", type=Path, help="decode the last frame to this PNG")
    ap.add_argument("--scan", type=float, default=15)
    ap.add_argument("--snap-every", type=float, default=0, help="`app`: save out/live-N.png every N s (0 = off)")
    ap.add_argument("--load", choices=["bench", "stream"], default="bench",
                    help="`stress`: synthetic `bench --kb` filler, or `stream on` (needs mirroring)")
    ap.add_argument("--every", type=float, default=0.2, help="`stress`: seconds between ping/move commands")
    a = ap.parse_args()

    async with BleakClient(await find_board(a.scan), timeout=20) as client:
        b = Board(client)
        await b.start()
        print("mtu:", client.mtu_size)
        if a.test in ("all", "info"):
            print("net:", await b.net())
            await t_status(b)
        if a.test in ("all", "bench"):
            await t_bench(b, a.kb)
        if a.test in ("all", "snap"):
            await t_snap(b, a.out / "snap.h264", a.png or a.out / "snap.png")
        if a.test in ("all", "stream"):
            await t_stream(b, a.seconds, a.out / "stream.h264", a.out / "stream.png")
        if a.test in ("all", "hid"):
            await t_hid(b)
        if a.test == "stress":
            await t_stress(b, a.seconds, a.load, a.kb, a.every)
        if a.test == "app":  # drive the phone's app through the board, print its log, snap now and then
            await t_app(b, a.lines, a.seconds, a.snap_every, a.out)
        for line in a.lines if a.test == "cmd" else []:
            reply, ms = await b.cmd(line, timeout=120)
            print(f"{line} -> {reply} ({ms:.0f} ms)")
        if a.test == "all":
            await t_status(b)


if __name__ == "__main__":
    asyncio.run(main())
