# Yappl

Yappl is a small voice-journaling device built around a Seeed Studio XIAO ESP32S3. It has an animated face, a microphone, a button, a reminder light, and a piezo. Recordings stream over Wi-Fi to a backend running on your computer, where they are saved, transcribed locally, and optionally summarized with an AI provider. You can play back recordings and read your journal in a browser.

Build and test the electronics on a breadboard first. Follow the configuration, upload, and backend steps below and complete a test recording before moving the electronics into the printed chassis.

## 1. Bill of materials

| Quantity | Part | Notes |
| --- | --- | --- |
| 1 | Seeed Studio XIAO ESP32S3 | Standard board with 8 MB flash and 8 MB PSRAM; connect its Wi-Fi antenna. |
| 1 | SH1107 128 × 128 I2C OLED | Address `0x3C`, compatible with 3.3 V power and logic. An SSD1306 or SPI display is not a drop-in replacement. |
| 1 | INMP441 I2S microphone breakout | External microphone; this firmware does not use the Sense board's built-in microphone. |
| 1 | Normally open momentary pushbutton | Use contacts that connect only when pressed. |
| 1 | LED | External status/reminder LED; choose your preferred color. |
| 1 | Small passive piezo element | Not an active buzzer, magnetic buzzer, or speaker. |
| 1 | Bare photoresistor (LDR) | Room-light sensor. |
| 1 | 330 Ω resistor | LED current limiting. |
| 1 | 100 Ω resistor | Piezo series resistor. |
| 2 | 10 kΩ resistors | One for the light-sensor divider and one for the button pulldown. |
| 1 | USB-C data cable | Powers the board and uploads firmware; a charge-only cable cannot upload. |
| As needed | Breadboard, jumper wires, and headers | For the initial working prototype. |
| As needed | Insulated hookup wire, solder, and heat-shrink | For final connections inside the chassis. |
| 1 set | Printed enclosure parts | Files and printing instructions below. |
| As needed | FDM filament | Your choice of colors; use different colors for the cap and dots. |

You will also need an FDM printer, a soldering iron and basic hand tools, and a computer to upload firmware and run the backend. A multimeter is useful for checking continuity before power-up. Keep the backend computer powered on and awake during recording.

## 2. Print the enclosure

I'm using a **Bambu A1**, but **any FDM printer should work**. Use Bambu Studio for the A1 or your printer's slicer.

Print one of each:

- [`cad/body.stl`](cad/body.stl)
- [`cad/chassis-bottom.stl`](cad/chassis-bottom.stl)
- [`cad/chassis-top.stl`](cad/chassis-top.stl)
- The assembled hat from [`cad/hat-parts/`](cad/hat-parts/): `main-cap.stl` and `dot1.stl` through `dot8.stl`.

For the hat:

1. Select **all nine meshes** in `cad/hat-parts/` and import them together.
2. When prompted, import them as **a single object with multiple parts**. Preserve their relative positions; do not arrange each mesh independently on the plate.
3. Expand that object in the slicer's object list. Assign `main-cap.stl` one filament color and the `dot[x].stl` parts a different color. **The colors can be whatever you like.** The dots can share a color or use several colors, as long as they contrast with the cap.
4. Slice and preview the hat to confirm that the dots are correctly positioned and have the intended colors.

Use your printer's material profile and print the models at their original scale. PLA and a 0.20 mm layer height are reasonable starting points, not a required or validated print profile. Check orientation, bed contact, overhangs, and any required supports in the preview before printing. Test-fit the chassis and your electronics before permanent assembly; module dimensions and printer tolerances can vary.

Automatic multicolor printing requires suitable filament-changing hardware, such as an AMS Lite on the A1. With a single-color printer, you can print the assembled hat in one color and paint the dots afterward.

## 3. Wire and test on a breadboard

**Disconnect USB before changing wiring.** Power the XIAO through USB-C. Connect its `3V3` pin to the breadboard positive rail and `GND` to the ground rail. All components must share ground. Use **3.3 V** for peripherals and signal wiring; do not connect 5 V to GPIO pins. Attach the board's Wi-Fi antenna.

The table uses the **D labels printed on the XIAO**. The numbers in [`include/app/config.h`](include/app/config.h) are GPIO numbers, which are different.

| Component terminal | Connection | GPIO |
| --- | --- | --- |
| OLED VCC | 3V3 | — |
| OLED GND | GND | — |
| OLED SDA | D4 | 5 |
| OLED SCL | D5 | 6 |
| INMP441 VDD / VCC | 3V3 | — |
| INMP441 GND | GND | — |
| INMP441 SCK / BCLK | D8 | 7 |
| INMP441 WS / LRCLK | D9 | 8 |
| INMP441 SD / DOUT | D10 | 9 |
| INMP441 L/R / SEL | GND (left channel) | — |
| LED anode (+, usually the long leg) | D6 through 330 Ω | 43 |
| LED cathode (−, usually the short leg/flat edge) | GND | — |
| Passive piezo + | D7 through 100 Ω | 44 |
| Passive piezo − | GND | — |
| Photoresistor first leg | 3V3 | — |
| Photoresistor second leg | D0, also connected through 10 kΩ to GND | 1 |
| Button contact A | 3V3 | — |
| Button contact B | D1, also connected through a separate 10 kΩ to GND | 2 |

The light divider is `3V3 → photoresistor → D0 → 10 kΩ → GND`: brighter light should increase the reading. The button is `3V3 → switch → D1 → 10 kΩ → GND`: pressed reads HIGH. On a four-leg tactile switch, two legs may already be connected internally; use contacts that are disconnected when released and connected when pressed.

Keep microphone wires short. D2 and D3 are unused. D6 and D7 are used by the LED and piezo, so serial monitoring uses USB-C. See [`docs/xiao_wiring.md`](docs/xiao_wiring.md) for additional wiring notes.

Leave the circuit on the breadboard while completing steps 4–7. Once everything works, transfer it to the chassis using step 8.

## 4. Configure your home network

Download or clone this repository and open its root folder (the folder containing `platformio.ini`). Install Visual Studio Code with the PlatformIO IDE extension, and install and start Docker Desktop for the backend. Run the `pio` commands below in a PlatformIO terminal; this makes them available even if PlatformIO is not on your system PATH. Run Docker commands in a terminal with the Docker CLI available.

### Find the backend computer's LAN address

Connect the computer and XIAO to the same home network. The XIAO needs **2.4 GHz Wi-Fi**. Avoid a guest network that isolates devices from each other.

Find the computer's local IPv4 address in your network settings or router's device list. For example:

- macOS: System Settings → Wi-Fi → Details → TCP/IP. `ipconfig getifaddr en0` also works when Wi-Fi uses `en0`.
- Windows: run `ipconfig` and use the IPv4 address of the active Wi-Fi/Ethernet adapter.
- Linux: run `ip -4 addr` and use the address of the active LAN interface.

For the examples below, assume it is `192.168.1.25`. **Replace this with your actual address.** Use the computer's LAN address, not the XIAO's address, a Docker container address, or `localhost`. A DHCP reservation in your router helps keep it stable.

### Set firmware credentials

Copy [`include/secrets.example.h`](include/secrets.example.h) to `include/secrets.h`. If that file already exists, edit it instead of overwriting your settings. Replace its contents with this minimal configuration and fill in your own values:

```cpp
#pragma once

#define YAPPL_WIFI_NETWORKS { {"YOUR_HOME_WIFI_NAME", "YOUR_WIFI_PASSWORD"} }
#define YAPPL_BACKEND_BASE_URLS { "http://192.168.1.25:8000" }
#define YAPPL_DEVICE_ID "yappl_dev_001"
#define YAPPL_DEVICE_SECRET "PASTE_YOUR_DEVICE_SECRET_HERE"
```

Use the exact Wi-Fi name and password, keeping the quotation marks. Escape any literal `"` or `\` in those values as `\"` or `\\`. Remove unused example networks and backend URLs. Multiple entries are supported, but one of each is enough for a home setup.

Choose a device secret and use the **same value** in the backend configuration below. You can generate one with `openssl rand -hex 32` in a terminal with OpenSSL installed. Keep the device ID stable; it identifies this device's stored sessions. Use a different ID for each additional device.

In [`include/app/config.h`](include/app/config.h), leave `enableWifi`, `enableBackend`, and `enableTimeSync` set to `true`. The supplied pin configuration matches the wiring above. The default `timeZone` is a POSIX TZ string for Chicago/Central time:

```cpp
static constexpr const char *timeZone = "CST6CDT,M3.2.0/2,M11.1.0/2";
```

If you live elsewhere, replace it with the POSIX TZ rule for your location; this field does not accept an IANA name such as `America/Chicago`. Reminder timing uses this local clock. Rebuild and upload after changing either firmware configuration file.

### Prepare the backend configuration

Copy `backend/.env.example` to `backend/.env`, or edit the existing `.env`. Set:

```dotenv
YAPPL_ENV=local
YAPPL_DEVICE_SECRET=PASTE_THE_SAME_DEVICE_SECRET_HERE
YAPPL_STORAGE_DIR=/data
YAPPL_MP3_BITRATE=64k
YAPPL_TRANSCRIPTION_ENABLED=true
YAPPL_WHISPER_LANGUAGE=en
YAPPL_TRANSCRIPTION_TIMEOUT_SECONDS=1800
YAPPL_SUMMARY_ENABLED=true
YAPPL_SUMMARY_TIMEOUT_SECONDS=120
YAPPL_SETTINGS_SECRET=PASTE_A_SEPARATE_RANDOM_SECRET_HERE
ANTHROPIC_API_KEY=PASTE_YOUR_ANTHROPIC_API_KEY_HERE
```

Generate a separate random value for `YAPPL_SETTINGS_SECRET`, for example with another `openssl rand -hex 32`. Keep it stable: it protects provider keys saved through the settings API. Keep `/data` as the container storage path; Docker maps it to `backend/data/` on your computer.

The repository defaults to Anthropic for summaries. Supply your own API key with access to the configured model; summaries send transcript content to that provider and may incur API charges. To use recordings and local English transcription without a summary provider, set `YAPPL_SUMMARY_ENABLED=false` and leave `ANTHROPIC_API_KEY` empty. Additional provider configuration is described in [`backend/README.md`](backend/README.md).

Both `include/secrets.h` and `backend/.env` are ignored by Git. Do not commit passwords or API keys.

## 5. Build and upload the firmware

Connect the breadboarded XIAO to your computer with the USB-C data cable. From the repository root in a PlatformIO terminal:

```sh
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -b 115200
```

The first build downloads the toolchain and libraries. Wait for a successful build and upload. If several serial devices are connected, run `pio device list`, then specify the XIAO port:

```sh
pio run -e seeed_xiao_esp32s3 -t upload --upload-port YOUR_PORT
pio device monitor -b 115200 --port YOUR_PORT
```

Replace `YOUR_PORT` with a port such as `/dev/cu.usbmodem…` on macOS or `COM3` on Windows. If upload cannot connect, hold BOOT while plugging in USB, release BOOT, and retry. Reset the board after uploading if needed; its serial port may change. Exit the monitor with Ctrl+C before another upload.

Backend connection failures are expected at this point because the server has not been started yet. Do not start a recording until the next step is complete.

## 6. Start the backend

With Docker Desktop running, open a second terminal at the repository root:

```sh
cd backend
docker compose up -d --build
docker compose ps
curl http://localhost:8000/health
```

The initial build compiles `whisper.cpp` and downloads the local English transcription model, so it requires internet access and may take several minutes. The `api` service should be running, and `/health` should return JSON containing `"status":"ok"`.

Open **http://localhost:8000** on the backend computer to see the journal dashboard. On another device on the same network, open **http://192.168.1.25:8000/health**, using your computer's actual LAN address. This checks reachability beyond the backend computer itself. If necessary, allow inbound TCP port 8000 through the computer's firewall on your private home network.

The local dashboard has no login. Keep it on a trusted LAN; do not port-forward it to the public internet.

Watch server logs from the `backend` directory:

```sh
docker compose logs -f api
```

Ctrl+C stops following logs; the server keeps running. After changing `.env`, run `docker compose up -d --force-recreate` from this directory to apply the new environment. To stop the server, use `docker compose down`. Saved sessions remain in `backend/data/`.

## 7. Verify the complete device

### Check startup and electronics

Reset the XIAO with the backend running and watch its serial monitor. Look for:

- `Hardware: flash=8 MB psram=ready` with roughly 8 MB of usable PSRAM and no hardware-profile warning.
- `INMP441 ready` and successful audio-buffer allocation.
- `Wi-Fi connected. IP: ...`.
- `NTP time synced: ...` or `Clock synced from backend: ...`.
- `Backend ping OK: HTTP 200` and `Backend status OK: HTTP 200`. Normal checks occur about every 30 seconds.

Confirm that the OLED displays a face. Cover and uncover the photoresistor: `light=...%` should fall and rise. Speak near the microphone: `mic=...%` should respond. Press and release the button: `button=...` should change in the logs. Check the external LED and piezo during activation and deactivation.

### Make a test recording at any time of day

1. While idle, hold the button for **5 seconds** to start a session. This also clears the device's stored completion marker so you can start again outside the normal reminder window. If already in `reminder` mode, a **0.5-second** hold starts it.
2. Wait for `mode=listening`, then release the button fully. Look for `Backend session start OK: HTTP 200`.
3. Speak for 10–20 seconds. Keep the backend running and the device connected to Wi-Fi while recording.
4. Hold the button again for **1 second**, then release it to finish. Look for `Backend session finish OK: HTTP 200` and `Audio session finished: ... bytes=... dropped=0`. The byte count should be nonzero.
5. Refresh the dashboard at **http://localhost:8000**. Confirm the new session appears, play its audio, and check that the voice and duration sound correct. Allow time for local transcription, then check the transcript and, if enabled, summary. Follow backend logs for processing errors.

Session files are saved under `backend/data/devices/<device_id>/sessions/`. A completed, successfully processed session should contain `audio.mp3`, transcript files, and (when summaries are enabled and successful) `summary.txt`. A successful `/health` response alone does not verify recording, transcription, or summary generation; this test checks the whole path.

For everyday use, the default reminder window starts at **8 PM local time** and ends at midnight. A reminder requires no completed-session history or more than **12 hours** since the last completed session. A short idle press gives a “not yet” response; use the deliberate five-second hold to start outside that window.

## 8. Install the electronics in the chassis

After the breadboard passes the checks above:

1. Disconnect USB. Photograph or label the working wiring before removing it from the breadboard.
2. Dry-fit the electronics into the printed chassis and body. Check the OLED alignment, button travel, USB-C access, microphone opening, LED visibility, and light-sensor exposure before securing anything.
3. Insert and secure the electronics, leaving access to their terminals. Route and solder the final wires using the **same connection table in step 3**. Transfer one connection at a time, keep microphone wiring short, and retain all four resistors and the shared ground.
4. Insulate exposed joints and arrange wires so they cannot be pinched when fitting the chassis pieces, body, and hat. Keep the microphone opening clear and avoid loading the antenna connector or USB socket.
5. With power still disconnected, check every connection against the table and check for shorts between 3V3 and GND. Confirm LED polarity and the button's contact pairs.
6. Power the device with the enclosure still accessible and repeat the startup, sensor, and recording checks in step 7. Then close the enclosure and repeat a short recording to confirm assembly has not obstructed the microphone, button, or light sensor.

The repository provides STL meshes rather than a documented fastener/assembly kit; check your component fit before choosing a permanent mounting method.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `pio` is not found | Use PlatformIO IDE's terminal in VS Code. |
| Upload fails or no USB port appears | Use a data cable, close the serial monitor, select the correct port, and try BOOT mode. |
| Hardware-profile warning | Confirm the standard XIAO ESP32S3 with 8 MB flash/PSRAM and the `seeed_xiao_esp32s3` build environment. |
| Wi-Fi fails | Check the antenna, 2.4 GHz network, SSID, and password in `include/secrets.h`; upload again and reset. |
| Backend requests fail | Check Docker, the computer's LAN IP, port 8000, firewall, and network isolation. Test `/health` from another LAN device. |
| Backend returns HTTP 401 | The firmware and backend `YAPPL_DEVICE_SECRET` values must match. Upload firmware and recreate the container after correcting them. |
| OLED is blank | Check SH1107/I2C compatibility, power, SDA/SCL, and address `0x3C`. |
| Mic is silent or audio is wrong | Check INMP441 power, D8/D9/D10 wiring, and L/R tied to GND. Inspect `mic` readings and play the saved recording. |
| Button or light reading is stuck | Check contact pairs, common ground, and the two separate 10 kΩ resistors. |
| Audio drops or recording is incomplete | Keep the backend awake and reachable; check Wi-Fi strength and logs. The device has only a short rolling audio buffer, not offline recording storage. |
| Transcript or summary is missing | Give processing time to finish and inspect `docker compose logs -f api`. Check provider credentials/model access for summaries, or confirm summaries were intentionally disabled. |
| Reminder time is wrong | Check `timeZone`, clock-sync logs, and the last completed session; rebuild/upload after firmware config changes. |

For implementation details, see the [system explainer](docs/system_explainer.md), [wiring reference](docs/xiao_wiring.md), and [backend guide](backend/README.md).
