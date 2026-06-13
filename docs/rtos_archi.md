# RTOS Architecture for Yappl

This note explains how FreeRTOS would help Yappl, specifically for the current
ESP32-S3 prototype with:

- SH1107 OLED
- INMP441 microphone
- Button
- Photoresistor
- Status LED
- Passive piezo buzzer

## The Current Problem

The current firmware is cooperative:

```text
loop()
  app.update()
    read button
    read photoresistor
    read mic
    update LED
    update piezo
    draw OLED
```

This works until one operation takes a long time.

The OLED is the main issue. The display is 128x128 monochrome:

```text
128 * 128 / 8 = 2048 bytes
```

The full display buffer is sent over I2C. At 400 kHz, that transfer can take
roughly 45-60 ms. While that happens, the normal Arduino `loop()` is blocked.

That means:

- LED PWM updates pause.
- Piezo note timing pauses.
- Button reads pause.
- Mic reads pause.

This is why the LED fade can look stepped and the piezo scale can sound uneven.

## What RTOS Changes

FreeRTOS lets the firmware run multiple tasks.

Instead of one loop doing everything, each behavior gets its own loop:

```text
Output task:
  updates LED and piezo

Sensor task:
  reads button, photoresistor, mic level

Display task:
  draws the OLED

Main loop:
  mostly idle
```

The OLED can still be slow, but it blocks only the display task. The output
task can keep running.

RTOS does not make the OLED faster. It prevents the OLED from holding the whole
application hostage.

## Mental Model

Think of each task as a small independent worker.

Each task has:

- A job.
- A priority.
- A repeat rate.
- A stack size.

Example:

```text
Output task:
  job: LED breathing and piezo rhythm
  priority: high
  rate: every 5 ms

Sensor task:
  job: read inputs and mic level
  priority: medium
  rate: every 20-50 ms

Display task:
  job: render OLED from latest state
  priority: low
  rate: every 100-250 ms
```

Higher priority tasks get CPU time first.

## Recommended Yappl Task Layout

### 1. Output Task

Purpose:

- Keep LED animation smooth.
- Keep piezo rhythm steady.

Runs often:

```text
every 5 ms
```

Owns:

- app state transitions
- LED state patterns
- piezo state melodies

Reads:

- `buttonPressed`

Writes:

- `ledBrightness`
- `piezoFrequencyHz`

Priority:

```text
high
```

Reason:

Human perception notices LED stutter and rhythm jitter quickly. This task should
not wait behind OLED drawing.

### 2. Sensor Task

Purpose:

- Read button.
- Read photoresistor.
- Read mic level.

Runs:

```text
every 20-50 ms
```

Owns:

- `Button`
- `Photoresistor`
- `Inmp441Microphone`

Writes:

- `buttonPressed`
- `lightRaw`
- `lightLevel`
- `micLevel`

Priority:

```text
medium
```

Reason:

Inputs should feel responsive, but they do not need to update every millisecond.

### 3. Display Task

Purpose:

- Draw current app state on the OLED.

Runs:

```text
every 100-250 ms
```

Owns:

- `OledDisplay`

Reads:

- Button status
- Light level
- Mic level
- LED brightness
- Piezo status

Priority:

```text
low
```

Reason:

OLED updates are slow. The display should never control the timing of audio,
LEDs, or input handling.

### 4. Main Arduino Loop

Purpose:

- Nothing important.

After RTOS tasks are started, `loop()` can be mostly idle:

```cpp
void loop() {
  delay(1000);
}
```

The real application is running in FreeRTOS tasks.

## Shared App State

Tasks need to communicate through shared state.

Example:

```cpp
struct AppState {
  bool buttonPressed;
  int lightRaw;
  uint8_t lightLevel;
  uint8_t micLevel;
  uint8_t ledBrightness;
  uint16_t piezoFrequencyHz;
};
```

The sensor task writes input values.

The output task reads `buttonPressed` and writes LED/piezo status.

The display task reads everything and draws it.

## Avoid Direct Task Coupling

Do not do this:

```text
Sensor task directly calls display.draw...
Display task directly calls piezo.play...
Output task directly reads photoresistor...
```

That makes timing and ownership messy.

Better:

```text
Drivers own hardware.
Tasks own behavior timing.
Shared state carries facts between tasks.
```

## Protecting Shared State

Because multiple tasks can access state at the same time, protect it.

For Yappl, the simplest good option is a mutex:

```text
lock state
copy or update values
unlock state
```

Pattern:

```cpp
AppState snapshot;

xSemaphoreTake(stateMutex, portMAX_DELAY);
snapshot = appState;
xSemaphoreGive(stateMutex);

display.draw(snapshot);
```

Important rule:

Do not hold the mutex while doing slow work.

Bad:

```cpp
xSemaphoreTake(stateMutex, portMAX_DELAY);
display.drawFaceFrame(...);  // slow OLED I2C write
xSemaphoreGive(stateMutex);
```

Good:

```cpp
xSemaphoreTake(stateMutex, portMAX_DELAY);
snapshot = appState;
xSemaphoreGive(stateMutex);

display.drawFaceFrame(...);  // slow, but state is unlocked
```

This lets other tasks keep updating state while the OLED is busy.

## Task Timing

Use `vTaskDelayUntil()` for steady timing.

Better:

```cpp
TickType_t lastWake = xTaskGetTickCount();

while (true) {
  updateSomething();
  vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5));
}
```

Worse:

```cpp
while (true) {
  updateSomething();
  delay(5);
}
```

`vTaskDelayUntil()` tries to keep a fixed schedule. That is better for rhythm
and animation.

## Suggested Priorities

Starting point:

```text
Output task priority: 3
Sensor task priority:   2
Display task priority:  1
```

Avoid very high priorities unless needed.

A task should finish quickly, then delay/yield. A high-priority task that never
delays can starve the rest of the system.

## Suggested Stack Sizes

Starting point:

```text
Output task: 3072 bytes
Sensor task:   4096 bytes
Display task:  6144 bytes
```

Display gets the most stack because graphics libraries often use more stack.

If the ESP32 crashes with stack overflow messages, increase the stack for that
task.

## Core Pinning

ESP32-S3 has two cores.

For now, avoid overthinking core pinning. Use `xTaskCreate()` first.

If needed later:

```text
Core 0:
  Wi-Fi / system work

Core 1:
  output, sensors, display
```

Arduino already runs on top of FreeRTOS, so core behavior can depend on the
Arduino core and Wi-Fi usage.

Use pinning only when there is a real reason.

## How YapplApp Would Change

Current role:

```text
YapplApp::update()
  does everything
```

RTOS role:

```text
YapplApp::begin()
  initializes drivers
  creates shared state
  creates tasks
```

Then:

```text
output task owns output timing
sensor task owns input timing
display task owns OLED timing
```

`YapplApp` becomes the system coordinator, not the place where every behavior
runs.

## Possible File Structure

```text
include/
  app/
    app_state.h
    yappl_app.h

  services/
    output_task.h
    sensor_task.h
    display_task.h
    act_player.h

src/
  app/
    app_state.cpp
    yappl_app.cpp

  services/
    output_task.cpp
    sensor_task.cpp
    display_task.cpp
    act_player.cpp
```

Drivers stay as hardware wrappers:

```text
drivers/
  button
  photoresistor
  status_led
  piezo_buzzer
  oled_display
  inmp441_microphone
```

## Practical Migration Plan

### Step 1: Add AppState

Create a central state struct:

```cpp
struct AppState {
  bool buttonPressed = false;
  int lightRaw = 0;
  uint8_t lightLevel = 0;
  uint8_t micLevel = 0;
  uint8_t ledBrightness = 0;
  uint16_t piezoFrequencyHz = 0;
};
```

Add a mutex next to it.

### Step 2: Move LED and Piezo Into Output Task

This should be the first RTOS task because it fixes the visible problem.

The output task:

- Reads `buttonPressed`.
- Updates the app mode.
- Updates LED state patterns.
- Updates piezo state melodies.
- Writes `ledBrightness` and `piezoFrequencyHz`.
- Runs every 5 ms.

### Step 3: Move OLED Into Display Task

The display task:

- Copies a snapshot of `AppState`.
- Draws the OLED from that snapshot.
- Runs every 100-250 ms.

This isolates the slow I2C display transfer.

### Step 4: Move Inputs Into Sensor Task

The sensor task:

- Reads button.
- Reads photoresistor.
- Reads mic level.
- Writes values into `AppState`.
- Runs every 20-50 ms.

### Step 5: Keep Loop Empty

Once tasks own the behavior:

```cpp
void loop() {
  delay(1000);
}
```

## Debugging RTOS Problems

Common problems:

### One task starves the others

Cause:

- A high-priority task does not delay.

Fix:

- Add `vTaskDelayUntil()`.
- Lower priority.

### OLED still causes jitter

Cause:

- OLED library or I2C driver may hold CPU longer than expected.

Fixes:

- Lower OLED refresh rate.
- Draw less often.
- Try partial redraws later.
- Keep output task higher priority.

### Random crashes

Possible causes:

- Stack too small.
- Shared state accessed without mutex.
- Calling a non-thread-safe driver from two tasks.

Fixes:

- Increase stack size.
- Use mutexes.
- Make sure each hardware driver is owned by only one task.

### Button feels delayed

Cause:

- Sensor task period too slow.

Fix:

- Use 10-20 ms sensor period.
- Add debounce logic.

## Rules for Managing the System

1. One task should own each hardware driver.
2. Slow hardware belongs in low-priority tasks.
3. Timing-sensitive behavior belongs in high-priority tasks.
4. Shared state should be copied quickly under a mutex.
5. Never hold a mutex while doing OLED drawing, mic reads, serial printing, or
   other slow work.
6. Every task must delay or block regularly.
7. Keep task responsibilities boring and narrow.

## Best First RTOS Version for Yappl

The first useful RTOS version should have only three tasks:

```text
output task
sensor task
display task
```

Do not split into many tiny tasks yet.

That is enough to prove the main goal:

```text
OLED can update slowly without ruining LED smoothness or piezo rhythm.
```
