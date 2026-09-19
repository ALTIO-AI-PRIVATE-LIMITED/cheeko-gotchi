# Getting Started

## What Developers Need

- Cheeko Gotchi device
- USB-C cable with data support
- macOS, Linux, or Windows
- ESP-IDF 5.5.x for firmware builds
- Optional cloud API key for cloud voice examples

## First Commands

```bash
./tools/cheeko doctor
./tools/cheeko ports
```

Expected output:

```text
Cheeko Doctor
Python: ok
ESP-IDF: found
Device port: /dev/cu.usbmodem101
Board metadata: ok
Cloud key: optional
```

## Developer Journey

1. Run `doctor`.
2. Flash the hardware test ([`firmware/hardware_test`](../../firmware/hardware_test/)) to check every part of the device.
3. Copy an example.
4. Implement `CheekoApp`.
5. Build and flash.
6. Monitor logs.

The final command shape should become:

```bash
./tools/cheeko run examples/hello_display
```

For v0.1, this repo defines the API and examples while the firmware integration
is being wrapped.

