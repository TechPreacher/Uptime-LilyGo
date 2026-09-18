---
title: Uptime LilyGo Agent Guide
description: Project context and working guidance for AI coding sessions
ms.date: 2026-09-18
ms.topic: reference
---

## Project summary

Uptime LilyGo is Arduino firmware for the LilyGo T-Display-S3 AMOLED 1.91-inch
board. It connects to Wi-Fi, checks configured HTTP and HTTPS endpoints, and
renders a landscape uptime dashboard with status totals, RSSI, UTC time, scan
progress, and refresh age.

The firmware is intentionally small. Most behavior lives in `src/main.cpp`.
Runtime configuration lives in LittleFS rather than firmware source.

## Hardware target

* LilyGo T-Display-S3 AMOLED 1.91-inch
* ESP32-S3 at 240 MHz
* 16 MB flash and 8 MB OPI PSRAM
* RM67162 AMOLED with native 240 x 536 resolution
* USB VID/PID `303A:1001`
* Custom PlatformIO board definition in `boards/T-Display-AMOLED.json`

The dashboard uses 536 x 240 landscape orientation. `amoled.setRotation(0)` is
correct for this LilyGo library and board combination. Do not change rotation
based only on generic RM67162 examples.

This board has no onboard speaker or buzzer. Sound alerts require external
hardware.

## Repository map

* `src/main.cpp`: settings parsing, Wi-Fi, endpoint checks, NTP, scheduling, and UI
* `platformio.ini`: PlatformIO environment, build flags, and pinned dependencies
* `boards/T-Display-AMOLED.json`: custom ESP32-S3 hardware definition
* `data/settings.ini.sample`: public configuration template
* `data/settings.ini`: private local configuration uploaded to LittleFS
* `lib/UptimeCore`: hardware-independent status, count, brightness, and timing logic
* `test/test_uptime_core`: host-side Unity tests for `UptimeCore`
* `media/uptime-lilygo.png`: project screenshot used by the README
* `README.md`: public setup and usage documentation

`include/README`, `lib/README`, and `test/README` are default PlatformIO
placeholders. Keep hardware-independent behavior in `UptimeCore` so it remains
testable without a connected board.

## Runtime behavior

Startup performs these operations:

1. Initialize the AMOLED and full-screen `TFT_eSprite` framebuffer.
2. Mount LittleFS and parse `/settings.ini`.
3. Apply configured brightness.
4. Connect to Wi-Fi.
5. Configure UTC time through NTP.
6. Check every configured endpoint and render results.

The main loop retries Wi-Fi every 15 seconds, refreshes endpoint status at the
configured interval, and redraws the dashboard once per second. Endpoint checks
are synchronous and use an eight-second timeout.

HTTP status codes from 200 through 399 count as healthy. Other status codes and
transport errors count as down. Redirects are followed. HTTPS currently uses
`WiFiClientSecure::setInsecure()`, so checks confirm availability but not server
identity.

## Configuration and secrets

Never commit, print, replace, or expose `data/settings.ini`. It contains Wi-Fi
credentials and potentially private URLs. The file is intentionally ignored by
Git. Preserve it unless the user explicitly requests a configuration change.

Keep `data/settings.ini.sample` secret-free and synchronized with supported
settings:

```ini
[wifi]
ssid = your-wifi-name
password = your-wifi-password

[sites]
Example = https://example.com

[config]
refresh_minutes = 5
display_brightness_percent = 75
```

Requirements and accepted ranges:

* `ssid` must not be empty
* At least one `[sites]` entry must exist
* `refresh_minutes` accepts 1 through 1440
* `display_brightness_percent` accepts 0 through 100

A settings-only change requires a LittleFS upload. It does not require firmware
recompilation.

## Implementation constraints

* Preserve Arduino framework and C++ compatibility provided by
  `espressif32@6.12.0`.
* Keep the LilyGo AMOLED dependency pinned unless an upgrade is requested and
  tested on hardware.
* Keep `-DLV_CONF_SKIP`; transitive LilyGo sources require it in this build.
* Render through the existing full-screen sprite and call `amoled.pushColors()`
  after drawing a complete frame.
* Keep dashboard geometry within 536 x 240 pixels.
* Avoid dynamic layout changes that cause redraw jitter or overlapping text.
* Preserve HTTP and HTTPS support unless requirements explicitly narrow it.
* Treat `data/settings.ini` and its flashed LittleFS image as separate from the
  firmware binary.

## Build and device commands

PlatformIO may not be on `PATH` in local AI sessions. Use the installed binary
directly when `pio` is unavailable:

```bash
$HOME/.platformio/penv/bin/platformio run
```

Run host-side unit tests:

```bash
$HOME/.platformio/penv/bin/platformio test -e native
```

Build and upload firmware:

```bash
$HOME/.platformio/penv/bin/platformio run --target upload
```

Upload LittleFS configuration:

```bash
$HOME/.platformio/penv/bin/platformio run --target uploadfs
```

Open serial monitor at 115200 baud:

```bash
$HOME/.platformio/penv/bin/platformio device monitor
```

The board has previously appeared as `/dev/cu.usbmodem1101`, but device paths
can change. Discover the connected port instead of assuming that value. Add
`--upload-port <port>` when automatic detection fails.

## Validation expectations

After firmware, board, or PlatformIO changes:

1. Run native tests with `platformio test -e native`.
2. Run a complete PlatformIO firmware build.
3. Check editor diagnostics for touched files.
4. Upload firmware when hardware behavior changed.
5. Upload LittleFS separately when configuration changed.
6. Confirm landscape rendering and endpoint totals on the physical display for
   UI or networking changes.

After documentation or sample configuration changes, verify that
`data/settings.ini` remains ignored and that no credentials appear in tracked
files.

## Version control

Repository remote is `git@github.com:TechPreacher/Uptime-LilyGo.git`, with
`main` as the published branch. This workspace uses GitButler for status,
commits, branches, and pushes. Do not commit or push unless the user requests
it, and never include `data/settings.ini` in any commit.
