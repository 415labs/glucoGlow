# glucoGlow

Bedside glucose display for the **ESP32-C3 0.42" OLED** board. Successor of [glucoLed2](https://github.com/415labs/glucoLed2) (Raspberry Pi + LED), rebuilt as a standalone device: it polls LibreLinkUp, shows the value, trend arrow and age of the reading on the built-in 72x40 OLED, and stays dim (but visible) at night. Everything is configured from a web app served by the device itself.

> **Not a medical device.** This is a convenience display. Keep your official CGM alarms enabled. It relies on the unofficial LibreLinkUp API, which can change without notice.

## Glucose ranges and colors
Four thresholds (defaults in mg/dL, all editable in the web app):

| Zone | Default | Color |
|---|---|---|
| **Critical low** | below 60 | red |
| **Caution** (low side) | 60 to 89 | orange |
| **Normal** | 90 to 150 | green |
| **Caution** (high side) | 151 to 200 | orange |
| **Critical high** | above 200 | red |

No data for 15 min (editable) = **stale** (blue).

**The OLED is a single-color (white) panel**, so zones are shown as a number plus a label (`CRIT LOW`, `LOW`, `HIGH`, `CRIT HIGH`, `STALE`) and an alert pattern. The web app shows them in color.

## Brightness
Day and night are never switched off: night is dim but always visible. In the web app set the day start / night start times, and the display brightness for day and night separately. Moving a brightness slider previews it on the device immediately.

## 12-hour chart
The web app shows a chart of the last 12 hours (one point per 5 minutes) with your ranges as colored bands. With **Display → Show 12-hour chart** on, the same chart also appears on the OLED for 4 s out of every 12 s while glucose is normal or caution (critical and stale alerts stay on the number). Turn the option off to hide both. The history lives in RAM; after a reboot or power loss the device pulls the last 12 hours from the LibreLink server on its first successful poll (needs the clock synced by NTP), then adds one point every 5 minutes.

## Screen rotation
Mount the board however fits your shelf: in the web app, **Display → Screen rotation**, choose 0, 90, 180 or 270 (degrees clockwise) and **Save**. It applies immediately. 90 and 270 turn the 72x40 panel into a tall 40x72 screen with its own layout (value on top, arrow, label, age below). If the board is upside down, the setup instructions are too until you set the rotation, so read them upside down once or turn the board for the first setup.

## Setup mode (change any setting at any time)
The device keeps showing glucose while you change settings.

1. **Hold the BOOT button for 3 seconds.** The screen counts down ("KEEP HOLD... in 3 s").
2. The screen then shows, one page at a time (it repeats):
   - **1/3** the WiFi to join: `glucoGlow-setup`
   - **2/3** the WiFi password: `glow` + 4 digits, generated per device
   - **3/3** the address to open: `192.168.4.1`
3. Join that WiFi from a phone or PC, enter the password, open `http://192.168.4.1`. Every setting is there.
4. Hold BOOT for 3 s again to leave setup mode. It also switches off by itself after 15 minutes without a connected device.

Other help on the device:
- At every power-up the screen shows "Hold BOOT 3 s, then follow the screens".
- **Tap BOOT** to cycle info screens: the IP address, the `glucoglow.local` name, and the setup how-to.
- If nothing is configured yet, the screen reminds you every 20 s how to start setup mode.
- If the saved WiFi can't be reached at boot (or none is saved), setup mode starts automatically and stays on until WiFi works. If the router was just slow to start, the device reboots and retries after 2 minutes with nobody connected.
- A critical low/high alert is never hidden behind setup screens.

Once the device is on your home WiFi you can also use `http://glucoglow.local` (or its IP) any time without setup mode.

## What it does
- Polls LibreLinkUp every **60 s by default** (30-900 s in the web app).
- Shows glucose (mg/dL or mmol/L), trend arrow and minutes since the reading.
- Alert pattern per zone (steady, blink slow/fast, slow/fast pulse, 3 blinks + pause). Critical animates only the **arrow** (default: fast pulse), caution animates only the **LOW / HIGH** label (default: slow pulse), no/stale data animates only the **STALE** text (default: slow pulse). The whole screen and the number never blink. On the monochrome OLED a pulse is a blink that stays on most of the time.
- The onboard blue LED is always off.

## Install (no tools needed)
Open **https://415labs.github.io/glucoglow/** in Chrome, Edge or Brave on Windows, Mac or Linux, plug in the board, click **Connect & install**. Then follow the on-screen setup steps. If the board isn't listed, hold its BOOT button while plugging it in.

## Build from source
Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e esp32-c3-devkitm-1 -t upload     # real firmware
pio device monitor                          # serial log, 115200
pio run -e mock -t upload                   # demo build: cycles fake readings, no LibreLinkUp needed
pio test -e native                          # unit tests for the pure logic (runs on your computer)
```

Board assumptions (confirm on yours): OLED SDA = GPIO5, SCL = GPIO6; BOOT = GPIO9; onboard LED = GPIO8. Change them in `src/display.cpp` / `src/main.cpp` if your board differs.

## First-time setup
1. Power the board. With no WiFi saved it starts setup mode by itself and walks you through it on the screen.
2. Join `glucoGlow-setup` with the password shown, open `http://192.168.4.1`.
3. Enter your WiFi and your **LibreLinkUp** email/password (a *follower* account that has the sensor data shared with it), press **Save**, then **Reboot**.
4. Afterwards open `http://glucoglow.local` (or the IP shown by tapping BOOT). **Test LibreLinkUp login** checks credentials without saving. The **Preview** box shows each zone on the display.

## Security notes
- The setup WiFi is protected by a per-device password (`glow` + 4 digits; WPA2 needs at least 8 characters) that is only shown on the device's own screen. Four digits is easy to type but short: it is meant for a few minutes of setup next to the device, and the setup WiFi switches itself off after 15 minutes without clients. You can set a longer password in the web app (Device section).
- Over your home WiFi the web app is plain HTTP; set an **admin password** (user `admin`) in the Device section. Reaching it through the setup WiFi does not ask for it (the WiFi password already protects that path).
- Passwords are write-only: the API never sends them back to the browser.
- HTTPS to LibreLinkUp validates certificates using the built-in CA bundle. If login fails with a network/TLS error, you can enable "Skip certificate check" in the web app (weaker security).

## WiFi transmit power
On the tested board the setup network was invisible at the default (maximum) transmit power and worked once power was capped at 8.5 dBm, so the firmware always applies that cap (`WIFI_TX_POWER` in `src/main.cpp`; override with a build flag, e.g. `-DWIFI_TX_POWER=WIFI_POWER_11dBm`). If your board still can't see or join networks, try a different value.

## Layout
- `src/main.cpp` main loop: WiFi, setup mode, BOOT button, NTP, polling and backoff, rendering
- `src/llu_client.*` LibreLinkUp login, region redirect, token reuse, latest reading
- `src/glucose.*` zones, colors, trend mapping, timestamp parsing, night schedule (unit tested)
- `src/settings.h`, `src/settings_store.*` settings struct, validation, NVS storage, JSON
- `src/display.*` OLED drawing, brightness schedule, alert animations, text screens
- `src/webui.*`, `src/webui_html.h` on-device web server, JSON API and single-page UI (embedded in firmware, no external assets)
- `test/test_glucose/` unit tests

## API (used by the web app)
`GET/POST /api/settings`, `GET /api/status`, `POST /api/test-login`, `POST /api/preview`, `POST /api/reboot`, `POST /api/factory-reset`.

## Known to verify on real hardware
- LibreLinkUp login headers and the account-id header; the unofficial API changes over time. If login is rejected as outdated, raise **LibreLinkUp app version** in the web app (default 4.16.0).
- The setup WiFi running alongside your home WiFi was not tested on hardware.
- Arrow and text layout on the 72x40 panel, and the text screens' line fit.


## Self-healing
If readings stop because of a network problem the device recovers on its own: it rejoins WiFi if the link stays down for 30 s, starts from a fresh LibreLinkUp login after 2 failed polls, and as a last resort restarts itself after 20 minutes without a reading (waiting longer after repeated restarts, up to 2 h). It never restarts for wrong credentials or a LibreLinkUp-side error, and never while the setup WiFi is in use or BOOT is held. The web app's Status box shows the last poll result and free memory; the serial monitor logs `[wifi]`, `[poll]` and `[watchdog]` lines.

## Releasing (maintainers)
Push a version tag and GitHub Actions builds the firmware, merges it into one `glucoglow-merged.bin`, attaches it to a GitHub Release and publishes the install page (`docs/`) on GitHub Pages:

```bash
git tag v0.3.0 && git push origin v0.3.0
```
One-time setup: repository **Settings -> Pages -> Source: GitHub Actions**.

## Disclaimer
Not a medical device. It relies on the unofficial LibreLinkUp API, which can change at any time. Keep your official CGM alerts on. MIT licensed, see `LICENSE`.
