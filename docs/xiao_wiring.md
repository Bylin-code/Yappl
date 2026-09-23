# Yappl wiring: Seeed Studio XIAO ESP32S3

Target: standard XIAO ESP32S3 (8 MB flash, 8 MB PSRAM), external INMP441,
128x128 SH1107 I2C OLED at address 0x3C, external LED, small passive piezo,
bare photoresistor, and normally open pushbutton. This firmware does not use
the Sense built-in PDM microphone, camera, or SD card. D8–D10 are assigned to
the external mic and cannot also serve the Sense SD card.

Disconnect USB before wiring. Use the board's printed pin labels; raw numbers
in `include/app/config.h` are GPIO numbers, not D numbers. Power the XIAO via
USB-C. Connect XIAO 3V3 to the breadboard positive rail and GND to its ground
rail. All components share this ground. Use 3.3 V for the peripherals and
signal pullups/dividers; do not connect 5 V to GPIO pins. Connect the supplied
antenna to the XIAO antenna socket for Wi-Fi.

## Connections

| Component terminal | Connect to | GPIO |
|---|---|---|
| OLED VCC | 3V3 rail | — |
| OLED GND | GND rail | — |
| OLED SDA | D4 | 5 |
| OLED SCL | D5 | 6 |
| INMP441 VDD / VCC | 3V3 rail | — |
| INMP441 GND | GND rail | — |
| INMP441 SCK / BCLK | D8 | 7 |
| INMP441 WS / LRCLK | D9 | 8 |
| INMP441 SD / DOUT | D10 | 9 |
| INMP441 L/R / SEL | GND rail (left channel) | — |
| External LED anode (+, usually long leg) | D6 through a 330 ohm resistor | 43 |
| External LED cathode (−, flat edge/short leg) | GND rail | — |
| Small passive piezo + | D7 through a 100 ohm series resistor | 44 |
| Small passive piezo − | GND rail | — |
| Photoresistor leg 1 | 3V3 rail | — |
| Photoresistor leg 2 | D0 and one end of a 10 kohm resistor | 1 |
| Other end of that 10 kohm resistor | GND rail | — |
| Button contact A | 3V3 rail | — |
| Button contact B | D1 and one end of a separate 10 kohm resistor | 2 |
| Other end of button's 10 kohm resistor | GND rail | — |

The photoresistor and resistors have no polarity. The photoresistor divider is
`3V3 -> photoresistor -> D0 -> 10 kohm -> GND`: brighter light raises the ADC
reading. The button is `3V3 -> switch -> D1 -> 10 kohm -> GND`: pressing reads
HIGH. On a four-leg tactile button, use two contacts that are disconnected
when released and connected when pressed; the two legs of each internally
connected pair are the same contact. Verify with continuity if unsure.

The piezo connection assumes a small passive piezo element, not a magnetic
buzzer, powered buzzer module, or speaker. Those require a suitable driver
circuit. The LED is external; the onboard LED is not used. The OLED must be
the existing SH1107 128x128 I2C module, not an SSD1306 or SPI display.

D2 and D3 are unused. D6 and D7 are repurposed for LED/piezo, so do not attach
a UART adapter there; serial logging uses USB-C. Keep microphone wires short.

## Build, upload, and check

```sh
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -b 115200
```

If automatic upload fails, hold BOOT while connecting USB, release BOOT, then
retry upload. Select the XIAO's USB port if more than one device is connected.
After upload, reset the board if needed and reopen the monitor if its port changes.

Startup should report 8 MB flash and 8 MB PSRAM. Check the OLED, button, LED,
piezo, light reading when covering/uncovering the sensor, and microphone
response to speech. Light thresholds and mic gain can be tuned in AppConfig.
Wi-Fi/backend credentials remain in `include/secrets.h`; the backend must be
reachable from the XIAO's network. A successful build does not verify wiring.

Compile the existing state-controller tests for this board with:

```sh
pio test -e xiao-tests --without-uploading --without-testing
```

To execute those tests on hardware, use `pio test -e xiao-tests`; this uploads
test firmware. Re-upload the normal application afterward.

Board references:
- [Seeed specifications and pinout](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [PlatformIO XIAO board](https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html)
