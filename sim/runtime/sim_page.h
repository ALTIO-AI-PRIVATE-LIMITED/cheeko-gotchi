#pragma once

// The simulator's browser UI, served at GET /. Self-contained: no external
// resources, talks to the sim over the same-origin HTTP endpoints:
//   GET  /frame        -> raw RGB framebuffer bytes (240*296*3)
//   GET  /events       -> newline-delimited runtime events
//   POST /input        -> "touch x y pressed" | "button name pressed" |
//                         "shake" | "tilt x y"
//   POST /text         -> body becomes OnCloudText() in the app
//   POST /fetchresult  -> first line fetch id, rest is the HTTP response body

namespace cheeko_sim {

static const char* const kSimPageHtml = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Cheeko Gotchi Simulator</title>
<style>
  :root { color-scheme: dark; }
  body { background:#12151c; color:#dfe6ef; font:14px/1.45 system-ui,Segoe UI,sans-serif;
         margin:0; display:flex; min-height:100vh; }
  .col { padding:18px; }
  #left { display:flex; flex-direction:column; align-items:center; gap:12px; }
  #shell { background:#ffb84d; border-radius:26px; padding:22px 18px 14px; box-shadow:0 8px 30px #0008; }
  canvas { display:block; width:480px; height:592px; image-rendering:pixelated;
           border-radius:8px; background:#000; cursor:crosshair; }
  .btnrow { display:flex; gap:8px; margin-top:12px; justify-content:center; }
  .btnrow button { background:#2a2f3a; color:#dfe6ef; border:1px solid #3d4452; border-radius:8px;
           padding:7px 12px; cursor:pointer; font-size:13px; }
  .btnrow button:active { background:#4a5264; }
  #right { flex:1; max-width:560px; display:flex; flex-direction:column; gap:10px; }
  h1 { font-size:16px; margin:0 0 4px; color:#ffd166; }
  .panel { background:#1a1e27; border:1px solid #2a2f3a; border-radius:10px; padding:10px 12px; }
  .panel h2 { font-size:12px; text-transform:uppercase; letter-spacing:.08em; color:#8b95a7; margin:0 0 6px; }
  #log, #cloud { height:170px; overflow-y:auto; font:12px/1.5 Consolas,monospace; white-space:pre-wrap; word-break:break-word; }
  #log .W { color:#ffd166; } #log .E { color:#ff6b6b; } #log .I { color:#9fd4a3; }
  #cloud .dev { color:#48d5ff; } #cloud .cloudmsg { color:#ffd166; }
  .tilt label { display:flex; align-items:center; gap:8px; font-size:12px; color:#8b95a7; }
  .tilt input[type=range] { flex:1; }
  #replyrow { display:flex; gap:6px; }
  #replybox { flex:1; background:#12151c; color:#dfe6ef; border:1px solid #3d4452; border-radius:6px; padding:6px 8px; }
  .hint { color:#8b95a7; font-size:12px; }
  label.chk { font-size:12px; color:#8b95a7; display:flex; gap:6px; align-items:center; }
</style>
</head>
<body>
<div class="col" id="left">
  <div id="shell">
    <canvas id="lcd" width="240" height="296"></canvas>
    <div class="btnrow">
      <button data-btn="voldown">VOL-</button>
      <button data-btn="power">POWER</button>
      <button data-btn="volup">VOL+</button>
      <button data-btn="boot">BOOT</button>
    </div>
  </div>
  <div class="btnrow">
    <button id="shakebtn">SHAKE</button>
    <span class="hint">click screen = touch, drag = swipe</span>
  </div>
  <div class="panel tilt" style="width:480px">
    <h2>Tilt (accelerometer)</h2>
    <label>X <input type="range" id="tiltx" min="-100" max="100" value="0"> <span id="tiltxv">0.00g</span></label>
    <label>Y <input type="range" id="tilty" min="-100" max="100" value="0"> <span id="tiltyv">0.00g</span></label>
    <div class="btnrow"><button id="tiltreset">level</button></div>
  </div>
</div>
<div class="col" id="right">
  <h1>Cheeko Gotchi Simulator</h1>
  <div class="panel"><h2>Device log</h2><div id="log"></div></div>
  <div class="panel">
    <h2>Cloud panel</h2>
    <div id="cloud"></div>
    <div id="replyrow">
      <input id="replybox" placeholder="type a cloud reply, sent to OnCloudText()">
      <button id="replysend">send</button>
    </div>
    <label class="chk"><input type="checkbox" id="autoreply" checked> auto-reply to SendText()</label>
    <div class="hint">GetJson()/PostJson() are fetched by this page; APIs without CORS return an error body.</div>
  </div>
</div>
<script>
"use strict";
const W = 240, H = 296, SCALE = 2;
const canvas = document.getElementById("lcd");
const ctx = canvas.getContext("2d");
const img = ctx.createImageData(W, H);
const logEl = document.getElementById("log");
const cloudEl = document.getElementById("cloud");

function post(path, body) {
  return fetch(path, { method: "POST", body: body }).catch(() => {});
}
function b64dec(s) {
  try { return decodeURIComponent(escape(atob(s))); } catch (e) { return atob(s); }
}
function addLine(el, text, cls) {
  const div = document.createElement("div");
  if (cls) div.className = cls;
  div.textContent = text;
  el.appendChild(div);
  while (el.childNodes.length > 400) el.removeChild(el.firstChild);
  el.scrollTop = el.scrollHeight;
}

async function pollFrame() {
  try {
    const buf = new Uint8Array(await (await fetch("/frame")).arrayBuffer());
    if (buf.length >= W * H * 3) {
      for (let i = 0, j = 0; i < W * H; i++) {
        img.data[i * 4] = buf[j++];
        img.data[i * 4 + 1] = buf[j++];
        img.data[i * 4 + 2] = buf[j++];
        img.data[i * 4 + 3] = 255;
      }
      ctx.putImageData(img, 0, 0);
    }
  } catch (e) { /* sim closed */ }
  setTimeout(pollFrame, 50);
}

let audio = null, volume = 80;
function beep(freq, dur) {
  if (!audio) audio = new (window.AudioContext || window.webkitAudioContext)();
  const osc = audio.createOscillator(), gain = audio.createGain();
  osc.type = "square"; osc.frequency.value = freq;
  const v = Math.max(0.001, volume / 100) * 0.18;
  gain.gain.setValueAtTime(v, audio.currentTime);
  gain.gain.exponentialRampToValueAtTime(0.001, audio.currentTime + dur / 1000);
  osc.connect(gain); gain.connect(audio.destination);
  osc.start(); osc.stop(audio.currentTime + dur / 1000 + 0.02);
}

async function doFetch(id, method, url, body) {
  let text;
  try {
    const res = await fetch(url, method === "POST"
      ? { method: "POST", headers: { "content-type": "application/json" }, body: body }
      : {});
    text = await res.text();
  } catch (e) {
    text = JSON.stringify({ error: "fetch_failed", message: String(e), url: url });
  }
  addLine(cloudEl, "-> " + method + " " + url, "dev");
  post("/fetchresult", id + "\n" + text);
}

async function pollEvents() {
  try {
    const text = await (await fetch("/events")).text();
    for (const line of text.split("\n")) {
      if (!line) continue;
      const p = line.split(" ");
      if (p[0] === "tone") { volume = +p[3]; beep(+p[1], +p[2]); }
      else if (p[0] === "log") addLine(logEl, "[" + p[1] + "] " + b64dec(p[2]), p[1]);
      else if (p[0] === "sent") {
        const msg = b64dec(p[1]);
        addLine(cloudEl, "app: " + msg, "dev");
        if (document.getElementById("autoreply").checked)
          setTimeout(() => post("/text", "(sim cloud) reply to: " + msg), 400);
      }
      else if (p[0] === "fetch") doFetch(p[1], "GET", b64dec(p[2]), "");
      else if (p[0] === "fetchpost") doFetch(p[1], "POST", b64dec(p[2]), b64dec(p[3]));
      else if (p[0] === "cloudtext") addLine(cloudEl, "cloud: " + b64dec(p[1]), "cloudmsg");
      else if (p[0] === "title") document.title = b64dec(p[1]) + " - Cheeko Sim";
    }
  } catch (e) { /* sim closed */ }
  setTimeout(pollEvents, 120);
}

let touchDown = false, lastMove = 0;
function touchPos(ev) {
  const r = canvas.getBoundingClientRect();
  const x = Math.max(0, Math.min(W - 1, Math.round((ev.clientX - r.left) / SCALE)));
  const y = Math.max(0, Math.min(H - 1, Math.round((ev.clientY - r.top) / SCALE)));
  return x + " " + y;
}
canvas.addEventListener("pointerdown", ev => {
  touchDown = true; canvas.setPointerCapture(ev.pointerId);
  post("/input", "touch " + touchPos(ev) + " 1");
});
canvas.addEventListener("pointermove", ev => {
  if (!touchDown || Date.now() - lastMove < 30) return;
  lastMove = Date.now();
  post("/input", "touch " + touchPos(ev) + " 1");
});
canvas.addEventListener("pointerup", ev => {
  if (!touchDown) return;
  touchDown = false;
  post("/input", "touch " + touchPos(ev) + " 0");
});

for (const btn of document.querySelectorAll("[data-btn]")) {
  btn.addEventListener("pointerdown", () => post("/input", "button " + btn.dataset.btn + " 1"));
  btn.addEventListener("pointerup", () => post("/input", "button " + btn.dataset.btn + " 0"));
}
document.getElementById("shakebtn").addEventListener("click", () => post("/input", "shake"));

const tx = document.getElementById("tiltx"), ty = document.getElementById("tilty");
function sendTilt() {
  document.getElementById("tiltxv").textContent = (tx.value / 100).toFixed(2) + "g";
  document.getElementById("tiltyv").textContent = (ty.value / 100).toFixed(2) + "g";
  post("/input", "tilt " + tx.value / 100 + " " + ty.value / 100);
}
tx.addEventListener("input", sendTilt);
ty.addEventListener("input", sendTilt);
document.getElementById("tiltreset").addEventListener("click", () => {
  tx.value = 0; ty.value = 0; sendTilt();
});

function sendReply() {
  const box = document.getElementById("replybox");
  if (box.value) { post("/text", box.value); box.value = ""; }
}
document.getElementById("replysend").addEventListener("click", sendReply);
document.getElementById("replybox").addEventListener("keydown", ev => {
  if (ev.key === "Enter") sendReply();
});

pollFrame();
pollEvents();
</script>
</body>
</html>
)HTML";

}  // namespace cheeko_sim
