# Yappl Roadmap

This is the recommended step-by-step path for building Yappl from the current prototype into the final AI journaling device.

## 1. Stabilize the Current Hardware

Get the current ESP32-S3, OLED, INMP441 mic, and MAX98357A amp working reliably.

Focus on:

- OLED meter draws correctly.
- Mic readings respond to speech.
- Speaker can play a simple sound.
- Firmware uploads and serial monitor are reliable.

Do not add Wi-Fi or backend work until the basic hardware loop is stable.

## 2. Add the Physical Controls

Add the real controls the product needs:

- One large button.
- Hidden on/off switch.
- Status LED or breathing light.

The button should start and stop a session. The LED should communicate idle, reminder, recording, and done states.

## 3. Add a Session State Machine

Create clear firmware states:

- Booting.
- Idle.
- Reminder active.
- Recording.
- Uploading.
- Done.
- Error.

Keep this local at first. The session can be fake before real audio upload exists.

## 4. Make Local Recording Work

Turn the mic from a level meter into a session recorder.

Start with:

- Button starts recording.
- Button stops recording.
- OLED shows recording state.
- OLED shows elapsed time.
- Firmware can hold or stream audio data without crashing.

At this stage, do not worry about AI.

## 5. Add Wi-Fi

Add Wi-Fi connection with hardcoded credentials first.

The device should:

- Connect on boot.
- Show failure clearly if Wi-Fi is unavailable.
- Reconnect if disconnected.
- Sync time with NTP.

Polished Wi-Fi provisioning can come later.

## 6. Create the First Backend

Build a small backend API.

Start with:

```text
GET /health
```

Then have the ESP32 call it and print/display whether the request worked.

Recommended early stack:

- Node.js + TypeScript backend.
- Render, Fly.io, Railway, or similar hosting.
- Supabase later for database and storage.

## 7. Add Device Authentication

Give the device a simple identity:

- `device_id`
- `device_secret`

The backend should reject requests without a valid device token.

Do not put OpenAI or other AI provider API keys on the ESP32.

## 8. Add Cloud Storage and Database

Set up the backend data model.

Likely tables:

```text
users
devices
journal_sessions
transcripts
summaries
questions
```

Use object storage for recordings:

```text
recordings/{user_id}/{session_id}/audio.wav
```

## 9. Upload Audio

Start with non-live upload.

Flow:

```text
button press -> start session
button press -> stop session
device uploads audio
backend stores recording
backend marks session complete
```

Live streaming can wait.

## 10. Add Transcription

After audio upload works, have the backend transcribe the recording.

Flow:

```text
audio uploaded
backend sends audio to transcription API
backend stores transcript
```

The device should not call the transcription provider directly.

## 11. Add Session Summaries

Use the transcript to generate:

- Short summary.
- Key events.
- Mood/theme tags.
- Notable people.
- Possible follow-up questions.

Store the result in the backend.

## 12. Add Nightly Reminder Logic

Use time and session history to decide whether to remind the user.

Behavior:

- If no session today after bedtime window, breathe LED and show reminder.
- If room lights are off and still no session, optionally flash LED.
- If already completed, stay idle and calm.

This can use backend session status or local cached state.

## 13. Build the Phone App

Build the app after transcripts and summaries exist.

Use Expo / React Native.

First app screens:

- Session list.
- Session summary.
- Raw transcript.
- Recording playback.
- Streak/progress view.

Do not build a complex app before the backend has real data.

## 14. Improve Device UI

Polish the bedside experience:

- Better OLED text wrapping.
- Better idle screen.
- Better recording screen.
- Better reminder animation.
- Better sound cues.
- Better error states.

The device should feel calm and intentional.

## 15. Add Live AI Questions

Only add this after the basic recording, upload, transcription, and summary loop works.

Target behavior:

```text
device records
backend receives live audio/transcript chunks
backend decides when a question is useful
device receives question
speaker makes gentle pop
OLED displays question
```

Avoid asking too many questions. The device should not interrupt the user.

## 16. Add Product-Level Setup

Before anyone else can use it, add:

- Wi-Fi provisioning.
- Device pairing.
- User accounts.
- Better auth.
- Firmware update strategy.
- Device reset flow.
- Export/delete controls.

## 17. Productize the Hardware

After the interaction works:

- Design enclosure.
- Mount button cleanly.
- Mount OLED cleanly.
- Tune LED brightness.
- Tune speaker volume.
- Make wiring robust.
- Add strain relief and power stability.

## Immediate Next Steps

1. Finish reliable OLED mic meter.
2. Add the button.
3. Add local session states.
4. Add Wi-Fi.
5. Add the first backend health check.

Backend, phone app, and live AI should come after the device has a solid local recording flow.
