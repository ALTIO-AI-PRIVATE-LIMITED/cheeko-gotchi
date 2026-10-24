/*
 * AirPlay screen-mirroring receiver for ESP32 (video only), ported from UxPlay
 * (https://github.com/FDH2/UxPlay, lib/), which builds on RPiPlay by Juho Vähä-Herttua et al.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { AIRPLAY_IDLE, AIRPLAY_CONNECTED, AIRPLAY_STREAMING };

typedef struct {
  const char *name;         // receiver name shown in Screen Mirroring
  uint8_t hwaddr[6];        // advertised deviceid
  uint8_t ed25519_seed[32]; // long-term pairing identity (keep stable across boots)
  uint16_t width, height;   // advertised display
  uint8_t max_fps;
  // H.264 out, Annex-B. Called from the mirror task; the data is only valid during the call.
  void (*on_config)(const uint8_t *sps_pps, size_t len);
  void (*on_au)(const uint8_t *au, size_t len, bool idr);
  void (*log)(const char *msg);  // optional handshake trace
} airplay_cfg;

// Advertises over mDNS and starts the RTSP server task. Call once Wi-Fi is up (any mode).
bool airplay_start(const airplay_cfg *cfg);
int airplay_state(void);  // AIRPLAY_IDLE / _CONNECTED / _STREAMING
// Display advertised in /info from the next mirroring session on.
void airplay_set_display(uint16_t width, uint16_t height, uint8_t max_fps);

#ifdef __cplusplus
}
#endif
