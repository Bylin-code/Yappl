# Yappl Project Plan

This document sketches a possible final project structure for Yappl. It is planning-only and does not imply these folders should exist yet.

## Final Repository Shape

```text
Yappl/
  README.md
  platformio.ini
  .gitignore
  docs/
    vision.md
    project_plan.md
    hardware.md
    api.md
    firmware_architecture.md
    backend_architecture.md
    product_notes.md

  firmware/
    include/
      app/
        app_config.h
        app_state.h
        yappl_app.h
      drivers/
        button.h
        led.h
        oled_display.h
        inmp441_microphone.h
      services/
        audio_recorder.h
        network_manager.h
        api_client.h
        reminder_scheduler.h
        journal_session.h
        ui_controller.h
      util/
        ring_buffer.h
        time_utils.h
        logging.h

    src/
      main.cpp
      app/
        yappl_app.cpp
        app_state.cpp
      drivers/
        button.cpp
        led.cpp
        oled_display.cpp
        inmp441_microphone.cpp
      services/
        audio_recorder.cpp
        network_manager.cpp
        api_client.cpp
        reminder_scheduler.cpp
        journal_session.cpp
        ui_controller.cpp
      util/
        time_utils.cpp
        logging.cpp

    test/
      test_audio_level.cpp
      test_reminder_scheduler.cpp
      test_journal_session.cpp

  backend/
    README.md
    package.json
    src/
      server.ts
      config/
        env.ts
      routes/
        device_auth.ts
        journal_sessions.ts
        audio_upload.ts
        transcripts.ts
        summaries.ts
      services/
        transcription_service.ts
        summary_service.ts
        storage_service.ts
        device_registry.ts
      db/
        schema.ts
        migrations/
      jobs/
        summarize_session.ts
        cleanup_uploads.ts
      types/
        journal.ts
        device.ts
        api.ts
      tests/

  web/
    README.md
    package.json
    src/
      app/
      components/
      pages/
      lib/
      styles/

  scripts/
    firmware_upload.sh
    firmware_monitor.sh
    provision_device.sh
    capture_serial_log.py
    analyze_audio_samples.py

  tools/
    manufacturing/
      flash_device.sh
      device_self_test.md
```

## Firmware Responsibilities

The firmware should stay focused on device behavior and hardware control.

### `main.cpp`

Tiny Arduino entry point.

Responsibilities:

- Create the app object.
- Call `begin()`.
- Call `update()`.

### `app/`

Owns the product-level state machine.

Expected states:

- Booting.
- Wi-Fi setup.
- Idle.
- Reminder active.
- Session ready.
- Recording.
- Uploading.
- Processing summary.
- Completed.
- Error.

### `drivers/`

Low-level hardware wrappers only.

Expected drivers:

- OLED display.
- INMP441 microphone.
- Button.
- LED or breathing light.

Drivers should not know about journaling, AI, reminders, or backend behavior.

### `services/`

Reusable firmware features that combine drivers or external systems.

Expected services:

- `audio_recorder`: manages sample collection and buffering.
- `network_manager`: owns Wi-Fi connection and reconnect behavior.
- `api_client`: talks to the cloud backend.
- `reminder_scheduler`: decides when the device should remind the user.
- `journal_session`: owns start/stop/upload/session lifecycle.
- `ui_controller`: decides what text or visuals appear on the OLED.

### `util/`

Small shared helpers.

Examples:

- Logging.
- Time utilities.
- Ring buffers.
- Safe string formatting.

## Backend Responsibilities

The backend should protect cloud/API credentials and own long-term data.

Expected responsibilities:

- Device authentication.
- Audio upload or streaming.
- Transcription.
- Journal session storage.
- Summary generation.
- User-facing API for future app/web UI.

The ESP32 should not contain private AI provider API keys.

## Web/App Responsibilities

The web or phone-facing layer can come later.

Expected responsibilities:

- View journal history.
- Read transcripts.
- Read summaries.
- Listen to saved audio.
- View streaks/progress.
- Configure reminder time.
- Manage device pairing.

## Suggested Milestones

1. Stabilize current OLED mic meter.
2. Add one-button session start/stop.
3. Add LED reminder hardware.
4. Add Wi-Fi connection.
5. Add backend health-check API call.
6. Add simple audio upload.
7. Add backend transcription.
8. Store raw audio and transcripts.
9. Add nightly reminder logic.
10. Add session summaries.
11. Add web/app journal viewer.

## Near-Term Cleanup Direction

Before the project grows much more:

- Keep product firmware separate from debug firmware.
- Keep drivers hardware-focused.
- Avoid putting backend/API logic directly in `main.cpp`.
- Avoid hardcoding real Wi-Fi or API secrets in committed files.
- Treat scripts as developer tools, not firmware behavior.

## Notes on Current Repository

The current project is still a PlatformIO firmware repo. A final product may become a monorepo with firmware, backend, web app, docs, and tools.

Do not create the full final scaffold until each major area is actually needed.
