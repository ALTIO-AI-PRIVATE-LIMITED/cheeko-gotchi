"use strict";

const PairingMethod = Object.freeze({
  QR_CODE: "qr_code",
  BLE_PROXIMITY: "ble_proximity",
  USB: "usb",
  MANUAL_CODE: "manual_code",
});

const JobStatus = Object.freeze({
  QUEUED: "queued",
  GENERATING: "generating",
  BUILDING: "building",
  SIGNING: "signing",
  READY: "ready",
  FAILED: "failed",
});

const DeploymentTransport = Object.freeze({
  WIFI_OTA: "wifi_ota",
  BLE_RELAY: "ble_relay",
  USB_RELAY: "usb_relay",
});

const Permission = Object.freeze({
  DISPLAY: "display",
  TOUCH: "touch",
  BUTTONS: "buttons",
  SPEAKER_TONE: "speaker.tone",
  SPEAKER_AUDIO: "speaker.audio",
  MIC_LEVEL: "mic.level",
  MIC_STREAM: "mic.stream",
  WIFI: "wifi",
  CLOUD_FETCH: "cloud.fetch",
  CLOUD_VOICE: "cloud.voice",
  STORAGE_LOCAL: "storage.local",
  NOTIFICATIONS: "notifications",
});

function json(statusCode, body) {
  return {
    statusCode,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
    },
    body: JSON.stringify(body, null, 2),
  };
}

function routeNotFound(method, pathname) {
  return json(404, {
    error: "not_found",
    message: `No route for ${method} ${pathname}`,
  });
}

module.exports = {
  DeploymentTransport,
  JobStatus,
  PairingMethod,
  Permission,
  json,
  routeNotFound,
};
