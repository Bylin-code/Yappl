Yappl hardware/software design notes
====================================

Purpose
-------

This project targets an ESP32-S3 board connected to:

  1. HiLetgo 1.5 inch SH1107 128x128 I2C OLED display
  2. INMP441 I2S digital microphone
  3. MAX98357A I2S amplifier connected to a speaker

The current app displays a live microphone level meter on the OLED. The
MAX98357A driver is present and has a sanity test tone API, but the main loop
does not currently play audio.


Hardware wiring
---------------

OLED: HiLetgo 1.5 inch SH1107 128x128 I2C OLED

  SCL: GPIO 11
  SDA: GPIO 12
  I2C address: 0x3C
  I2C speed: 400 kHz
  Reset pin: not connected / not used

INMP441 microphone, I2S input

  SCK/BCLK: GPIO 4
  WS/LRCLK: GPIO 5
  SD/DOUT:  GPIO 6
  LR:       GND, so use left channel

MAX98357A amplifier, I2S output

  BCLK: GPIO 15
  LRC:  GPIO 16
  DIN:  GPIO 17


Current module structure
------------------------

main/main.c

  Owns the current application behavior.
  Initializes the OLED and INMP441 mic.
  Reads mic samples continuously.
  Converts mic signal span to a 0-100 level.
  Sends the level to the OLED meter drawing API.

main/inmp441.h and main/inmp441.c

  API:

    esp_err_t inmp441_init(uint32_t sample_rate_hz);
    esp_err_t inmp441_read(int32_t *samples, size_t sample_count, size_t *samples_read);
    esp_err_t inmp441_sanity_check(void);
    esp_err_t inmp441_deinit(void);

  Current configuration:

    I2S port: I2S_NUM_0
    Role: master
    Format: standard Philips I2S
    Data width: 32-bit
    Slot mode: mono
    Slot mask: left channel
    Sample rate used by main: 16000 Hz

  INMP441 returns 24-bit data in a 32-bit slot. The current app uses raw
  int32_t samples and computes min/max span rather than converting to 16-bit
  PCM.

main/max98357a.h and main/max98357a.c

  API:

    esp_err_t max98357a_init(uint32_t sample_rate_hz);
    esp_err_t max98357a_write(const int16_t *samples, size_t sample_count, size_t *samples_written);
    esp_err_t max98357a_sanity_check(void);
    esp_err_t max98357a_deinit(void);

  Current configuration:

    I2S port: I2S_NUM_1
    Role: master
    Format: standard Philips I2S
    Data width: 16-bit
    Slot mode: mono

  The sanity check writes a short 440 Hz square wave tone at 16000 Hz sample
  rate. It is intended only as a quick audible wiring check.

main/ssd1306_oled.h and main/ssd1306_oled.c

  Historical file name says ssd1306_oled, but the actual display is SH1107.
  Rename this module in a future cleanup if desired.

  API:

    esp_err_t ssd1306_oled_init(void);
    esp_err_t ssd1306_oled_clear(void);
    esp_err_t ssd1306_oled_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const uint8_t *bitmap);
    esp_err_t ssd1306_oled_draw_meter(uint8_t level);
    esp_err_t ssd1306_oled_sanity_check(void);
    esp_lcd_panel_handle_t ssd1306_oled_panel(void);

  Current display configuration:

    Controller: SH1107
    Resolution: 128x128
    I2C address: 0x3C
    Pixel format: 1 bit per pixel
    Buffer size: 128 * 128 / 8 = 2048 bytes
    X/Y swap: enabled
    Color inversion: enabled

  ESP-IDF component used:

    esp_lcd_sh1107

  For ESP-IDF, this dependency is declared in:

    main/idf_component.yml

  PlatformIO equivalent:

    Use an SH1107-capable display library or driver. Do not use an SSD1306
    128x64 driver for this display. If using Arduino/PlatformIO, likely library
    choices are U8g2 or Adafruit_GFX plus an SH1107-compatible driver. Configure
    for SH1107 128x128 I2C, address 0x3C, SCL 11, SDA 12.


Current app behavior
--------------------

On boot:

  1. Initialize SH1107 OLED.
  2. Initialize INMP441 mic at 16000 Hz.
  3. Enter an infinite loop.

Loop:

  1. Read 256 int32_t mic samples.
  2. Find min and max sample values.
  3. Compute:

       span = max - min - NOISE_FLOOR

  4. Clamp span to 0..NOISE_CEILING.
  5. Convert to:

       level = span * 100 / NOISE_CEILING

  6. Draw a horizontal meter on the OLED.
  7. Delay 50 ms.

Constants currently used:

  SAMPLE_RATE_HZ    = 16000
  MIC_SAMPLE_COUNT  = 256
  NOISE_FLOOR       = 1000
  NOISE_CEILING     = 120000
  OLED width        = 128
  OLED height       = 128

The mic level algorithm is intentionally simple. It is a visual responsiveness
test, not calibrated SPL or VU metering.


Display buffer format
---------------------

The OLED drawing code uses a page-packed monochrome buffer:

  buffer size = width * height / 8
  byte index  = page * width + x
  page        = y / 8

Each byte represents 8 vertical pixels in one column. This is common for
SH1107/SSD1306-style monochrome OLED controllers.

The current meter drawing uses whole page bytes to avoid messy per-pixel
orientation issues. If porting to another graphics library like U8g2, it is
fine to replace this with normal drawing calls such as drawFrame and drawBox.

Expected display output:

  A simple horizontal audio level meter. The filled bar should grow when the
  mic hears louder sound and shrink when the environment is quieter.

If the display is rotated, mirrored, or transposed:

  Try toggling the display driver's X/Y swap, mirror X, mirror Y, and inversion
  settings. In this ESP-IDF implementation, X/Y swap is currently enabled.


Sanity checks
-------------

Available sanity check APIs:

  ssd1306_oled_sanity_check()

    Draws a checker pattern. Use this to verify the OLED driver, dimensions,
    and orientation.

  inmp441_sanity_check()

    Reads 256 samples and logs min, max, and span. A very small span likely
    means the mic is not wired correctly, the wrong channel is selected, or the
    room is very quiet. Since LR is grounded, use the left channel.

  max98357a_sanity_check()

    Plays a short 440 Hz square-wave tone. If no tone is heard, check BCLK,
    LRC, DIN, power, ground, speaker connection, and amp gain/shutdown pins.

Suggested bring-up order:

  1. OLED only: init display and run checker pattern.
  2. Mic only: init mic and log sample span while tapping/clapping near it.
  3. Amp only: init amp and play the short test tone.
  4. Full app: read mic and render live meter.


PlatformIO porting notes
------------------------

The current code is ESP-IDF style. In PlatformIO, choose one of these approaches:

  1. PlatformIO with framework = espidf

     This is the closest port. Keep the module split and use ESP-IDF I2S/I2C
     APIs. Add an SH1107 display component/library equivalent to
     esp_lcd_sh1107, or replace the display module with another SH1107 driver.

  2. PlatformIO with framework = arduino

     Replace ESP-IDF driver calls with Arduino-compatible libraries:

       OLED: U8g2 or another SH1107 128x128 I2C library
       Mic:  ESP32 I2S driver or Arduino I2S wrapper
       Amp:  ESP32 I2S driver or Arduino I2S wrapper

     Preserve the same module boundaries:

       inmp441_init/read/sanity_check
       max98357a_init/write/sanity_check
       oled_init/clear/draw_meter/sanity_check

Use separate I2S peripherals or channels if possible:

  Mic currently uses I2S port 0.
  Amp currently uses I2S port 1.

This avoids trying to share one I2S bus across two different pin sets.


Important gotchas
-----------------

1. The OLED is SH1107 128x128, not SSD1306 128x64.

   Using an SSD1306 driver or 128x64 buffer causes scrambled/unorganized output.

2. INMP441 LR is grounded.

   Select left channel.

3. GPIO 6 is mic SD.

   Do not reuse GPIO 6 for an LED or other output.

4. The MAX98357A module has no readback.

   A software "connected" check is not possible through I2S alone. The practical
   sanity test is playing a tone and listening for it.

5. The mic level meter is uncalibrated.

   Tune NOISE_FLOOR and NOISE_CEILING based on real readings from your board.

6. X/Y swap is currently enabled for the SH1107.

   If the display orientation is wrong in the final library, try flipping this
   setting first.


Current ESP-IDF build dependencies
----------------------------------

main/CMakeLists.txt currently registers:

  main.c
  max98357a.c
  inmp441.c
  ssd1306_oled.c

and requires:

  esp_driver_i2s
  esp_driver_i2c
  esp_lcd

main/idf_component.yml declares:

  esp_lcd_sh1107: "^1"

The current ESP-IDF build successfully fetched:

  espressif/esp_lcd_sh1107 1.2.0


Suggested final architecture
----------------------------

Keep the project split into these conceptual modules:

  display module:
    init the SH1107
    clear the screen
    draw simple UI/meter primitives

  mic module:
    init INMP441 I2S input
    read signed sample blocks
    optionally expose a helper for signal level

  amp module:
    init MAX98357A I2S output
    write PCM samples
    provide a test tone helper

  app module:
    owns the product behavior
    combines mic readings with OLED output
    later can route/generated audio to the amp
