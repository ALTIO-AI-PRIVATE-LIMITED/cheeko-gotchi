// Cheeko Gotchi desktop simulator.
//
// Implements the whole public SDK surface (sdk/include/cheeko.h) on a host
// machine: the 240x296 screen is served to a browser page over a tiny local
// HTTP server, touch/buttons/tilt/shake come back from the page, tones play
// through WebAudio, and cloud().GetJson()/PostJson() are fetched by the page.
//
// Build (one app at a time, the CLI does this for you):
//   g++ -std=gnu++14 -O2 -I sdk/include -I sdk/runtime \
//       sim/runtime/cheeko_sim.cc examples/hello_display/src/app.cc \
//       -lws2_32 -o cheeko-sim.exe          (drop -lws2_32 off Windows)
//
// Single-threaded by design: old MinGW toolchains lack std::thread, and the
// app contract forbids blocking callbacks anyway. Each loop iteration pumps
// pending HTTP requests, dispatches queued events, ticks the app, sleeps.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
typedef SOCKET sim_sock_t;
#define SIM_INVALID_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sim_sock_t;
#define SIM_INVALID_SOCK (-1)
#endif

#include "cheeko.h"
#include "cheeko_font.h"
#include "sim_page.h"

extern "C" cheeko::CheekoApp* CreateCheekoApp();

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 296;
constexpr int kFrameBytes = kWidth * kHeight * 3;

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

uint64_t NowMs() {
#ifdef _WIN32
  // GetTickCount wraps after ~49 days; irrelevant for a dev simulator, and
  // GetTickCount64 is missing from old MinGW.org headers.
  return GetTickCount();
#else
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

void SleepMs(int ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

std::string Base64Encode(const std::string& in) {
  static const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t chunk = (uint8_t)in[i] << 16;
    if (i + 1 < in.size()) chunk |= (uint8_t)in[i + 1] << 8;
    if (i + 2 < in.size()) chunk |= (uint8_t)in[i + 2];
    out += alphabet[(chunk >> 18) & 63];
    out += alphabet[(chunk >> 12) & 63];
    out += i + 1 < in.size() ? alphabet[(chunk >> 6) & 63] : '=';
    out += i + 2 < in.size() ? alphabet[chunk & 63] : '=';
  }
  return out;
}

std::string Base64Decode(const std::string& in) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int buffer = 0, bits = 0;
  for (char c : in) {
    int v = value(c);
    if (v < 0) continue;
    buffer = (buffer << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((buffer >> bits) & 0xff);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Simulator state
// ---------------------------------------------------------------------------

struct SimState {
  uint8_t framebuffer[kFrameBytes];
  cheeko::TouchEvent touch;
  float tilt_x = 0.0f;
  float tilt_y = 0.0f;
  bool shaken = false;
  int volume = 80;
  bool headless = false;
  uint64_t started_ms = 0;
  uint32_t sim_uptime_ms = 0;  // headless mode advances this deterministically
  int fetch_counter = 0;
  cheeko::CheekoApp* app = nullptr;
  std::vector<std::string> events;
  std::vector<std::string> pending_cloud_texts;
  std::map<std::string, std::string> storage;
  std::string storage_path = "cheeko-storage.txt";
};

SimState g_state;

uint32_t UptimeMs() {
  if (g_state.headless) return g_state.sim_uptime_ms;
  return (uint32_t)(NowMs() - g_state.started_ms);
}

void PushEvent(const std::string& event) {
  if (g_state.headless) return;
  if (g_state.events.size() > 500) {
    g_state.events.erase(g_state.events.begin(), g_state.events.begin() + 250);
  }
  g_state.events.push_back(event);
}

void QueueCloudText(const std::string& text) {
  g_state.pending_cloud_texts.push_back(text);
  PushEvent("cloudtext " + Base64Encode(text));
}

void SaveStorage() {
  std::ofstream out(g_state.storage_path.c_str(), std::ios::trunc);
  for (const auto& entry : g_state.storage) {
    out << Base64Encode(entry.first) << " " << Base64Encode(entry.second) << "\n";
  }
}

void LoadStorage() {
  std::ifstream in(g_state.storage_path.c_str());
  std::string key, value;
  while (in >> key >> value) {
    g_state.storage[Base64Decode(key)] = Base64Decode(value);
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void PutPixel(int x, int y, uint32_t rgb) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  uint8_t* p = &g_state.framebuffer[(y * kWidth + x) * 3];
  p[0] = (rgb >> 16) & 0xff;
  p[1] = (rgb >> 8) & 0xff;
  p[2] = rgb & 0xff;
}

void FillRectPx(int x, int y, int w, int h, uint32_t rgb) {
  int x0 = std::max(0, x), y0 = std::max(0, y);
  int x1 = std::min(kWidth, x + w), y1 = std::min(kHeight, y + h);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      uint8_t* p = &g_state.framebuffer[(yy * kWidth + xx) * 3];
      p[0] = (rgb >> 16) & 0xff;
      p[1] = (rgb >> 8) & 0xff;
      p[2] = rgb & 0xff;
    }
  }
}

// 5x7 glyphs at 2x scale: 12px advance, 14px tall, matching the device runtime.
constexpr int kTextScale = 2;
constexpr int kCharAdvance = (cheeko::kFontWidth + 1) * kTextScale;

void DrawTextPx(int x, int y, const std::string& text, uint32_t rgb) {
  int cx = x;
  for (char c : text) {
    const uint8_t* glyph = cheeko::FontGlyph(c);
    for (int col = 0; col < cheeko::kFontWidth; ++col) {
      for (int row = 0; row < cheeko::kFontHeight; ++row) {
        if (glyph[col] & (1 << row)) {
          FillRectPx(cx + col * kTextScale, y + row * kTextScale, kTextScale,
                     kTextScale, rgb);
        }
      }
    }
    cx += kCharAdvance;
  }
}

int TextWidthPx(const std::string& text) {
  if (text.empty()) return 0;
  return (int)text.size() * kCharAdvance - kTextScale;
}

void WriteBmp(const std::string& path) {
  const int row = kWidth * 3;  // 720, already 4-byte aligned
  const int data = row * kHeight;
  const int file = 54 + data;
  uint8_t header[54] = {0};
  header[0] = 'B'; header[1] = 'M';
  header[2] = file & 0xff; header[3] = (file >> 8) & 0xff;
  header[4] = (file >> 16) & 0xff; header[5] = (file >> 24) & 0xff;
  header[10] = 54;
  header[14] = 40;
  header[18] = kWidth & 0xff; header[19] = (kWidth >> 8) & 0xff;
  header[22] = kHeight & 0xff; header[23] = (kHeight >> 8) & 0xff;
  header[26] = 1;
  header[28] = 24;
  header[34] = data & 0xff; header[35] = (data >> 8) & 0xff;
  header[36] = (data >> 16) & 0xff; header[37] = (data >> 24) & 0xff;
  std::ofstream out(path.c_str(), std::ios::binary);
  out.write((const char*)header, sizeof(header));
  for (int y = kHeight - 1; y >= 0; --y) {
    for (int x = 0; x < kWidth; ++x) {
      const uint8_t* p = &g_state.framebuffer[(y * kWidth + x) * 3];
      char bgr[3] = {(char)p[2], (char)p[1], (char)p[0]};
      out.write(bgr, 3);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// SDK implementation
// ---------------------------------------------------------------------------

namespace cheeko {

int Display::Width() { return kWidth; }

int Display::Height() { return kHeight; }

void Display::Clear(uint32_t rgb) { FillRectPx(0, 0, kWidth, kHeight, rgb); }

void Display::Text(int x, int y, const std::string& text) {
  DrawTextPx(x, y, text, Color::Ink);
}

void Display::CenterText(int y, const std::string& text) {
  DrawTextPx((kWidth - TextWidthPx(text)) / 2, y, text, Color::Ink);
}

void Display::Rect(int x, int y, int w, int h, uint32_t rgb) {
  FillRectPx(x, y, w, 1, rgb);
  FillRectPx(x, y + h - 1, w, 1, rgb);
  FillRectPx(x, y, 1, h, rgb);
  FillRectPx(x + w - 1, y, 1, h, rgb);
}

void Display::FillRect(int x, int y, int w, int h, uint32_t rgb) {
  FillRectPx(x, y, w, h, rgb);
}

void Display::Circle(int x, int y, int radius, uint32_t rgb) {
  int dx = radius, dy = 0, err = 1 - radius;
  while (dx >= dy) {
    PutPixel(x + dx, y + dy, rgb); PutPixel(x - dx, y + dy, rgb);
    PutPixel(x + dx, y - dy, rgb); PutPixel(x - dx, y - dy, rgb);
    PutPixel(x + dy, y + dx, rgb); PutPixel(x - dy, y + dx, rgb);
    PutPixel(x + dy, y - dx, rgb); PutPixel(x - dy, y - dx, rgb);
    ++dy;
    if (err < 0) {
      err += 2 * dy + 1;
    } else {
      --dx;
      err += 2 * (dy - dx) + 1;
    }
  }
}

void Display::FillCircle(int x, int y, int radius, uint32_t rgb) {
  for (int dy = -radius; dy <= radius; ++dy) {
    int span = (int)std::sqrt((double)(radius * radius - dy * dy));
    FillRectPx(x - span, y + dy, span * 2 + 1, 1, rgb);
  }
}

void Display::Line(int x1, int y1, int x2, int y2, uint32_t rgb) {
  int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
  int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    PutPixel(x1, y1, rgb);
    if (x1 == x2 && y1 == y2) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x1 += sx; }
    if (e2 <= dx) { err += dx; y1 += sy; }
  }
}

void Display::Image(const std::string& path) {
  FillRectPx(20, 20, kWidth - 40, 40, 0x2a2f3a);
  DrawTextPx(28, 32, "[image] " + path, Color::Amber);
  Cheeko().log().Warn("Display::Image is not supported in the simulator yet: " + path);
}

void Speaker::Play(const std::string& path) {
  // Sim-only preview of the future PCM/voice pipeline: a "say:<text>" path is
  // spoken via the browser's text-to-speech. On the device this logs until
  // asset playback lands, so apps must treat voice as a bonus, not a feature.
  if (path.compare(0, 4, "say:") == 0) {
    PushEvent("say " + Base64Encode(path.substr(4)));
    return;
  }
  Cheeko().log().Info("Speaker::Play(" + path + ") — asset playback is simulated as a tone");
  PushEvent("tone 660 180 " + std::to_string(g_state.volume));
}

void Speaker::PlayPcm(const int16_t* samples, size_t sample_count,
                      int sample_rate_hz) {
  if (samples == nullptr || sample_count == 0) return;
  // Wrap the samples in a WAV container and hand it to the page's WebAudio.
  const uint32_t data_bytes = (uint32_t)(sample_count * 2);
  const uint32_t byte_rate = (uint32_t)sample_rate_hz * 2;
  std::string wav;
  wav.reserve(44 + data_bytes);
  auto u32 = [&wav](uint32_t v) {
    wav += (char)(v & 0xff); wav += (char)((v >> 8) & 0xff);
    wav += (char)((v >> 16) & 0xff); wav += (char)((v >> 24) & 0xff);
  };
  auto u16 = [&wav](uint16_t v) {
    wav += (char)(v & 0xff); wav += (char)((v >> 8) & 0xff);
  };
  wav += "RIFF"; u32(36 + data_bytes); wav += "WAVEfmt ";
  u32(16); u16(1); u16(1); u32((uint32_t)sample_rate_hz); u32(byte_rate);
  u16(2); u16(16);
  wav += "data"; u32(data_bytes);
  wav.append((const char*)samples, data_bytes);
  PushEvent("pcm " + Base64Encode(wav));
}

void Speaker::Tone(int frequency_hz, int duration_ms) {
  PushEvent("tone " + std::to_string(frequency_hz) + " " +
            std::to_string(duration_ms) + " " + std::to_string(g_state.volume));
}

void Speaker::SetVolume(int volume) {
  g_state.volume = std::max(0, std::min(100, volume));
}

void Microphone::Start(int sample_rate_hz) {
  Cheeko().log().Warn("Microphone capture is not implemented in the simulator; OnMicAudio will not fire (requested " +
                      std::to_string(sample_rate_hz) + "Hz)");
}

void Microphone::Stop() {}

TouchEvent Touch::Get() { return g_state.touch; }

bool Touch::IsPressed() { return g_state.touch.pressed; }

MotionSample Motion::Read() {
  MotionSample sample;
  sample.x = g_state.tilt_x;
  sample.y = g_state.tilt_y;
  float rest = 1.0f - sample.x * sample.x - sample.y * sample.y;
  sample.z = rest > 0.0f ? std::sqrt(rest) : 0.0f;
  return sample;
}

bool Motion::IsShaken() {
  bool value = g_state.shaken;
  g_state.shaken = false;
  return value;
}

void Storage::PutInt(const std::string& key, int value) {
  g_state.storage[key] = std::to_string(value);
  SaveStorage();
}

int Storage::GetInt(const std::string& key, int fallback) {
  auto it = g_state.storage.find(key);
  return it == g_state.storage.end() ? fallback : std::atoi(it->second.c_str());
}

void Storage::PutString(const std::string& key, const std::string& value) {
  g_state.storage[key] = value;
  SaveStorage();
}

std::string Storage::GetString(const std::string& key, const std::string& fallback) {
  auto it = g_state.storage.find(key);
  return it == g_state.storage.end() ? fallback : it->second;
}

bool Storage::Has(const std::string& key) {
  return g_state.storage.count(key) > 0;
}

void Storage::Remove(const std::string& key) {
  g_state.storage.erase(key);
  SaveStorage();
}

void Log::Info(const std::string& message) {
  std::printf("[I] %s\n", message.c_str());
  PushEvent("log I " + Base64Encode(message));
}

void Log::Warn(const std::string& message) {
  std::printf("[W] %s\n", message.c_str());
  PushEvent("log W " + Base64Encode(message));
}

void Log::Error(const std::string& message) {
  std::printf("[E] %s\n", message.c_str());
  PushEvent("log E " + Base64Encode(message));
}

void Cloud::Connect() {
  Cheeko().log().Info("cloud connected (simulated)");
}

void Cloud::StartVoiceSession() {
  Cheeko().log().Info("voice session requested — type a reply in the cloud panel");
  QueueCloudText("(sim) voice session open. Type a reply in the browser cloud panel.");
}

void Cloud::SendText(const std::string& text) {
  PushEvent("sent " + Base64Encode(text));
  if (g_state.headless) {
    QueueCloudText("(sim cloud) reply to: " + text);
  }
}

void Cloud::GetJson(const std::string& url) {
  int id = ++g_state.fetch_counter;
  PushEvent("fetch " + std::to_string(id) + " " + Base64Encode(url));
}

void Cloud::PostJson(const std::string& url, const std::string& json) {
  int id = ++g_state.fetch_counter;
  PushEvent("fetchpost " + std::to_string(id) + " " + Base64Encode(url) + " " +
            Base64Encode(json));
}

void Wifi::Connect() {
  Cheeko().log().Info("wifi connected (simulated)");
}

bool Wifi::IsConnected() const { return true; }

int Wifi::Scan(WifiNetwork* out, int max_count) {
  static const struct {
    const char* ssid;
    int rssi;
    bool secured;
  } kFakeNetworks[] = {
      {"CheekoNet", -46, true},
      {"Home-2.4G", -58, true},
      {"CafeGuest", -71, false},
      {"NextDoor", -84, true},
  };
  int count = 0;
  for (const auto& fake : kFakeNetworks) {
    if (count >= max_count) break;
    out[count].ssid = fake.ssid;
    out[count].rssi = fake.rssi;
    out[count].secured = fake.secured;
    ++count;
  }
  Cheeko().log().Info("wifi scan (simulated): " + std::to_string(count) +
                      " networks");
  return count;
}

void Wifi::SetCredentials(const std::string& ssid, const std::string&) {
  Cheeko().log().Info("wifi credentials saved (simulated) for '" + ssid + "'");
}

CheekoRuntime& Cheeko() {
  static CheekoRuntime runtime;
  return runtime;
}

Display& CheekoRuntime::display() { static Display d; return d; }
Speaker& CheekoRuntime::speaker() { static Speaker s; return s; }
Microphone& CheekoRuntime::mic() { static Microphone m; return m; }
Touch& CheekoRuntime::touch() { static Touch t; return t; }
Motion& CheekoRuntime::motion() { static Motion m; return m; }
Storage& CheekoRuntime::storage() { static Storage s; return s; }
Log& CheekoRuntime::log() { static Log l; return l; }
Cloud& CheekoRuntime::cloud() { static Cloud c; return c; }
Wifi& CheekoRuntime::wifi() { static Wifi w; return w; }

}  // namespace cheeko

// ---------------------------------------------------------------------------
// HTTP server (single-threaded, request-per-connection)
// ---------------------------------------------------------------------------

namespace {

sim_sock_t g_listen_sock = SIM_INVALID_SOCK;

void CloseSock(sim_sock_t sock) {
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

void SetNonBlocking(sim_sock_t sock, bool enabled) {
#ifdef _WIN32
  u_long mode = enabled ? 1 : 0;
  ioctlsocket(sock, FIONBIO, &mode);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

void SetRecvTimeout(sim_sock_t sock, int ms) {
#ifdef _WIN32
  DWORD timeout = ms;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

bool StartServer(int port) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
  g_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (g_listen_sock == SIM_INVALID_SOCK) return false;
  int one = 1;
  setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = htons((unsigned short)port);
  if (bind(g_listen_sock, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
  if (listen(g_listen_sock, 16) != 0) return false;
  SetNonBlocking(g_listen_sock, true);
  return true;
}

void SendAll(sim_sock_t sock, const char* data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    int n = (int)send(sock, data + sent, (int)(size - sent), 0);
    if (n <= 0) return;
    sent += (size_t)n;
  }
}

void Respond(sim_sock_t sock, const char* status, const char* content_type,
             const char* body, size_t body_size) {
  std::ostringstream head;
  head << "HTTP/1.1 " << status << "\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body_size << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "Connection: close\r\n\r\n";
  std::string head_str = head.str();
  SendAll(sock, head_str.c_str(), head_str.size());
  SendAll(sock, body, body_size);
}

void RespondText(sim_sock_t sock, const char* status, const std::string& body) {
  Respond(sock, status, "text/plain; charset=utf-8", body.c_str(), body.size());
}

cheeko::ButtonEvent::Button ButtonFromName(const std::string& name) {
  if (name == "power") return cheeko::ButtonEvent::Button::Power;
  if (name == "volup") return cheeko::ButtonEvent::Button::VolumeUp;
  if (name == "voldown") return cheeko::ButtonEvent::Button::VolumeDown;
  return cheeko::ButtonEvent::Button::Boot;
}

void HandleInput(const std::string& body) {
  std::istringstream in(body);
  std::string kind;
  in >> kind;
  if (kind == "touch") {
    int x = 0, y = 0, pressed = 0;
    in >> x >> y >> pressed;
    g_state.touch.x = x;
    g_state.touch.y = y;
    g_state.touch.pressed = pressed != 0;
    if (g_state.app) g_state.app->OnTouch(g_state.touch);
  } else if (kind == "button") {
    std::string name;
    int pressed = 0;
    in >> name >> pressed;
    cheeko::ButtonEvent event;
    event.button = ButtonFromName(name);
    event.pressed = pressed != 0;
    if (g_state.app) g_state.app->OnButton(event);
  } else if (kind == "shake") {
    g_state.shaken = true;
    if (g_state.app) g_state.app->OnShake();
  } else if (kind == "tilt") {
    in >> g_state.tilt_x >> g_state.tilt_y;
  }
}

void HandleRequest(sim_sock_t sock) {
  SetNonBlocking(sock, false);
  SetRecvTimeout(sock, 800);

  std::string request;
  char buffer[4096];
  size_t header_end = std::string::npos;
  while (header_end == std::string::npos && request.size() < 65536) {
    int n = (int)recv(sock, buffer, sizeof(buffer), 0);
    if (n <= 0) { CloseSock(sock); return; }
    request.append(buffer, (size_t)n);
    header_end = request.find("\r\n\r\n");
  }
  if (header_end == std::string::npos) { CloseSock(sock); return; }

  std::istringstream first(request.substr(0, request.find("\r\n")));
  std::string method, path;
  first >> method >> path;

  size_t content_length = 0;
  {
    std::string lower = request.substr(0, header_end);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t pos = lower.find("content-length:");
    if (pos != std::string::npos) {
      content_length = (size_t)std::atol(lower.c_str() + pos + 15);
    }
  }
  std::string body = request.substr(header_end + 4);
  while (body.size() < content_length) {
    int n = (int)recv(sock, buffer, sizeof(buffer), 0);
    if (n <= 0) break;
    body.append(buffer, (size_t)n);
  }

  if (method == "GET" && path == "/") {
    Respond(sock, "200 OK", "text/html; charset=utf-8", cheeko_sim::kSimPageHtml,
            std::strlen(cheeko_sim::kSimPageHtml));
  } else if (method == "GET" && path == "/frame") {
    Respond(sock, "200 OK", "application/octet-stream",
            (const char*)g_state.framebuffer, kFrameBytes);
  } else if (method == "GET" && path == "/events") {
    std::string joined;
    for (const auto& event : g_state.events) joined += event + "\n";
    g_state.events.clear();
    RespondText(sock, "200 OK", joined);
  } else if (method == "POST" && path == "/input") {
    HandleInput(body);
    RespondText(sock, "200 OK", "ok");
  } else if (method == "POST" && path == "/text") {
    QueueCloudText(body);
    RespondText(sock, "200 OK", "ok");
  } else if (method == "POST" && path == "/fetchresult") {
    size_t newline = body.find('\n');
    QueueCloudText(newline == std::string::npos ? body : body.substr(newline + 1));
    RespondText(sock, "200 OK", "ok");
  } else {
    RespondText(sock, "404 Not Found", "not found");
  }
  CloseSock(sock);
}

void PumpServer() {
  for (int i = 0; i < 32; ++i) {
    sim_sock_t client = accept(g_listen_sock, nullptr, nullptr);
    if (client == SIM_INVALID_SOCK) return;
    HandleRequest(client);
  }
}

void OpenBrowser(int port) {
  std::string url = "http://localhost:" + std::to_string(port) + "/";
#ifdef _WIN32
  std::system(("start \"\" \"" + url + "\"").c_str());
#elif defined(__APPLE__)
  std::system(("open \"" + url + "\"").c_str());
#else
  std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  int port = 8123;
  int ticks = 120;
  bool open_browser = true;
  std::string screenshot;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (arg == "--ticks" && i + 1 < argc) ticks = std::atoi(argv[++i]);
    else if (arg == "--no-open") open_browser = false;
    else if (arg == "--storage" && i + 1 < argc) g_state.storage_path = argv[++i];
  }

  g_state.headless = !screenshot.empty();
  g_state.started_ms = NowMs();
  LoadStorage();
  std::memset(g_state.framebuffer, 0, sizeof(g_state.framebuffer));

  g_state.app = CreateCheekoApp();
  g_state.app->OnStart();

  if (g_state.headless) {
    for (int i = 0; i < ticks; ++i) {
      g_state.sim_uptime_ms += 16;
      g_state.app->OnTick(g_state.sim_uptime_ms);
      for (const auto& text : g_state.pending_cloud_texts) {
        g_state.app->OnCloudText(text);
      }
      g_state.pending_cloud_texts.clear();
    }
    WriteBmp(screenshot);
    std::printf("wrote %s after %d ticks\n", screenshot.c_str(), ticks);
    g_state.app->OnStop();
    return 0;
  }

  if (!StartServer(port)) {
    std::fprintf(stderr, "cheeko-sim: could not listen on port %d\n", port);
    return 1;
  }
  std::printf("Cheeko Gotchi simulator: http://localhost:%d/\n", port);
  std::printf("Ctrl+C to quit. Storage file: %s\n", g_state.storage_path.c_str());
  if (open_browser) OpenBrowser(port);

  while (true) {
    PumpServer();
    if (!g_state.pending_cloud_texts.empty()) {
      std::vector<std::string> texts;
      texts.swap(g_state.pending_cloud_texts);
      for (const auto& text : texts) g_state.app->OnCloudText(text);
    }
    g_state.app->OnTick(UptimeMs());
    SleepMs(12);
  }
}
