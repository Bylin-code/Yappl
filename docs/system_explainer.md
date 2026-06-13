# Yappl System Explainer

This document explains how the current Yappl firmware is structured and how the
RTOS version works.

It is written for someone new to RTOS concepts.

## Big Picture

Yappl currently runs on an ESP32-S3 using Arduino on top of FreeRTOS.

Arduino still provides:

- `setup()`
- `loop()`
- `pinMode()`
- `digitalRead()`
- `analogRead()`
- `tone()`
- `analogWrite()`

FreeRTOS provides:

- tasks
- task priorities
- task delays
- mutexes

The key change is this:

Old architecture:

```text
One loop did everything.
```

Current architecture:

```text
Several RTOS tasks run at the same time.
Each task owns one kind of work.
```

## Why RTOS Was Added

The OLED is slow.

The SH1107 OLED uses a 128x128 monochrome buffer:

```text
128 * 128 / 8 = 2048 bytes
```

Sending that buffer over I2C takes a noticeable amount of time.

When everything was inside one `loop()`, the OLED update blocked everything
else:

```text
OLED drawing starts
LED updates pause
piezo note changes pause
button reads pause
mic reads pause
OLED drawing ends
everything resumes
```

That caused:

- choppy LED fading
- uneven piezo rhythm

RTOS fixes this by letting OLED drawing happen in a low-priority task while the
LED and piezo continue in a higher-priority task.

## Current Task Split

The firmware now has four important execution areas:

```text
Arduino main loop
Output task
Sensor task
Display task
```

### Arduino Main Loop

File:

```text
src/main.cpp
```

The Arduino code is still the entry point:

```cpp
void setup() {
  app.begin();
}

void loop() {
  app.update();
}
```

But `app.update()` no longer runs the whole product.

It only idles:

```cpp
void YapplApp::update() {
  delay(1000);
}
```

This is intentional. The real work now happens in RTOS tasks.

### Output Task

The output task controls things the user sees/hears immediately:

- LED brightness
- piezo scale

It runs often:

```text
every 5 ms
```

This task needs to be smooth, so it has the highest priority of the Yappl tasks.

It owns these behavior services:

```text
state transitions
LED patterns
piezo melodies
```

The output task reads:

```text
buttonPressed
```

It writes:

```text
ledBrightness
piezoFrequencyHz
```

### Sensor Task

The sensor task reads inputs:

- button
- photoresistor
- microphone level

It runs:

```text
every 50 ms
```

It writes:

```text
buttonPressed
lightRaw
lightLevel
micLevel
```

This task is medium priority.

It is allowed to take longer than the output task because mic reads can block for
milliseconds.

### Display Task

The display task owns the OLED.

It runs:

```text
every 100 ms
```

It reads the latest state and draws:

- button state
- light level
- mic level
- LED brightness
- piezo status

This task is low priority because OLED drawing is slow.

The important rule:

```text
OLED drawing should not control LED or piezo timing.
```

## File Structure

Current important files:

```text
src/main.cpp
  Arduino entry point.

include/app/yappl_app.h
src/app/yappl_app.cpp
  Initializes hardware.
  Creates RTOS tasks.
  Owns shared app state and mutex.

include/app/app_state.h
  Defines the shared AppState struct.

include/app/config.h
  Hardware pins, feature flags, task periods, priorities, stack sizes.

include/drivers/
src/drivers/
  Low-level hardware wrappers.

include/services/
src/services/
  Behavior logic that sits above drivers.
```

## Drivers vs Services vs App

The project has three layers.

### Drivers

Drivers are low-level hardware wrappers.

Examples:

```text
Button
Photoresistor
StatusLed
PiezoBuzzer
OledDisplay
Inmp441Microphone
```

Drivers answer questions like:

```text
How do I read this pin?
How do I set this LED brightness?
How do I send text to this OLED?
How do I read mic samples?
```

Drivers should not know product behavior.

For example, `StatusLed` should not know what "recording" means.

It only knows:

```cpp
setBrightness(...)
```

### Services

Services contain behavior.

Current services:

```text
ActPlayer
```

`ActPlayer` knows which OLED face animation should play for the current app mode.

Services use drivers, but they still should not own the whole product.

### App

`YapplApp` coordinates the system.

It:

- initializes drivers
- creates the shared state mutex
- starts RTOS tasks
- owns the task entry functions

`YapplApp` is the glue.

It is not supposed to contain every detail of LED animation, piezo note order,
or OLED drawing.

## Shared AppState

Tasks need to share information.

That shared information lives in:

```text
include/app/app_state.h
```

Current state:

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

This is the current "truth" that tasks share.

Example:

```text
Sensor task reads the button.
Sensor task writes buttonPressed = true.
Output task reads buttonPressed = true.
Output task starts LED breathing and piezo scale.
Display task reads buttonPressed = true.
Display task draws "Button: PRESSED".
```

## Why a Mutex Exists

Multiple tasks can access `AppState`.

Without protection, this could happen:

```text
Display task starts reading AppState.
Sensor task changes AppState halfway through.
Display task gets a mixed old/new state.
```

That is called a race condition.

The project uses a mutex to prevent this.

A mutex is like a lock.

Only one task can hold the lock at a time.

Pattern:

```cpp
xSemaphoreTake(stateMutex_, portMAX_DELAY);
state_ = newValues;
xSemaphoreGive(stateMutex_);
```

The current app uses helper methods:

```cpp
stateSnapshot()
publishSensorState(...)
publishOutputState(...)
```

Those helpers hide the lock/unlock details.

## Important Mutex Rule

Do not hold the mutex while doing slow work.

Bad idea:

```text
lock state
draw OLED
unlock state
```

That would block other tasks from updating state while the OLED is doing a slow
I2C transfer.

Good idea:

```text
lock state
copy state into local snapshot
unlock state
draw OLED from local snapshot
```

That is what the display task does.

## How Each Task Runs

Each task is a function with an infinite loop:

```cpp
while (true) {
  doWork();
  waitUntilNextPeriod();
}
```

The current code uses:

```cpp
vTaskDelayUntil(...)
```

This means:

```text
Run this task on a steady schedule.
```

That is better than a normal delay for rhythms and animations.

## Task Periods

Task timing is configured in:

```text
include/app/config.h
```

Current periods:

```cpp
outputTaskPeriodMs = 5;
sensorTaskPeriodMs = 50;
displayTaskPeriodMs = 100;
serialLogMs = 500;
```

Meaning:

```text
Output task updates LED/piezo every 5 ms.
Sensor task reads sensors every 50 ms.
Display task redraws OLED every 100 ms.
Serial output prints every 500 ms.
Piezo changes notes every 180 ms.
```

## Task Priorities

Priorities are also in:

```text
include/app/config.h
```

Current priorities:

```cpp
outputTaskPriority = 3;
sensorTaskPriority = 2;
displayTaskPriority = 1;
```

Higher number means higher priority.

So:

```text
Output task runs before sensor task.
Sensor task runs before display task.
Display task runs last.
```

This is intentional.

OLED drawing is slow, so it gets the lowest priority.

LED/piezo timing should feel smooth, so output gets the highest priority.

## Task Stacks

Each task needs memory for its local variables and function calls.

This memory is called stack.

Current stack sizes:

```cpp
outputTaskStackBytes = 3072;
sensorTaskStackBytes = 4096;
displayTaskStackBytes = 6144;
```

Display gets the most stack because graphics libraries can use more memory.

If a task stack is too small, the ESP32 may crash or reboot.

## How Startup Works

Startup flow:

```text
Arduino calls setup()
setup() calls app.begin()
app.begin() starts Serial
app.begin() creates state mutex
app.begin() initializes OLED/mic/LED/piezo/photoresistor/button
app.begin() writes initial AppState
app.begin() starts output task
app.begin() starts sensor task
app.begin() starts display task
loop() idles forever
```

After that, the tasks run independently.

## How Button Press Behavior Works

When the button is not pressed:

```text
Sensor task writes buttonPressed = false.
Output task sees false.
Output task turns LED off.
Output task stops piezo.
Display task shows button open.
```

When the button is pressed:

```text
Sensor task writes buttonPressed = true.
Output task sees true.
Output task updates LED breathing every 5 ms.
Output task advances piezo notes on its own note clock.
Display task shows button pressed and current values.
```

The button does not directly control the LED.

Instead:

```text
button -> AppState -> output behavior
```

This keeps the system clean.

## Why the OLED Should Be Low Priority

The OLED is slow because it sends a lot of bytes over I2C.

If OLED had high priority, it could still hurt smooth output.

So the display task:

- copies state quickly
- releases the mutex
- spends time drawing
- waits until next display period

The output task can keep running between display work.

## How to Manage This System

Use these rules:

1. Keep drivers simple.
2. Put behavior in services.
3. Let tasks own timing.
4. Share facts through `AppState`.
5. Protect shared state with the mutex.
6. Do not hold the mutex during slow hardware calls.
7. Give slow work lower priority.
8. Give smooth/timing-sensitive work higher priority.
9. Every task must delay regularly.

## When Adding a New Feature

Ask these questions:

### Is it low-level hardware?

Put it in:

```text
drivers/
```

Example:

```text
new LED chip
new button type
new sensor
```

### Is it behavior?

Put it in:

```text
services/
```

Example:

```text
LED reminder pattern
button debounce
session timer
piezo sound pattern
```

### Does it need to share information?

Add fields to:

```text
AppState
```

Example:

```cpp
bool recording;
bool wifiConnected;
uint32_t sessionElapsedSec;
```

### Does it block or need special timing?

Consider a task.

Examples that deserve their own task later:

- audio recording
- Wi-Fi upload
- OLED display

Examples that probably do not need their own task:

- one LED pin
- one button pin
- one simple ADC read

## Common Mistakes

### Mistake: One task per tiny hardware part

This creates too many tasks and too much coordination.

Better:

```text
input task
output task
display task
audio task
network task
```

### Mistake: Tasks directly call each other

Avoid:

```text
sensor task tells display task what to draw
display task tells output task what sound to play
```

Better:

```text
tasks read/write shared AppState
product state decides behavior
```

### Mistake: Holding mutex during OLED drawing

This blocks other tasks from sharing state.

Always copy state first, then draw.

### Mistake: High-priority task never waits

If a high-priority task never calls `vTaskDelayUntil()` or otherwise blocks, it
can starve lower-priority tasks.

Every task loop needs a delay.

## Current System Summary

```text
Button/photoresistor/mic
  read by sensor task
  written into AppState

LED/piezo
  controlled by output task
  based on AppState.buttonPressed
  status written back into AppState

OLED
  controlled by display task
  draws a snapshot of AppState

Main loop
  idles
```

This structure keeps the slow OLED from ruining the timing of the LED and
piezo, while keeping the code organized enough to grow into the final Yappl
device.
