# Yappl Vision

## One-Sentence Description

Yappl is a bedside journalist that entices you to yap to it for about five minutes to record day-to-day life, with AI-powered summaries and progress tracking in the cloud.

## Audience

The first version is for personal use. The long-term hope is to turn it into a product for others.

## Personality and Feel

Yappl should feel like a playful journaling companion.

It should quietly record the user's journal entry without interrupting their train of thought. The OLED should communicate device state, reminders, recording progress, and completion.

The device will have a name and personality. It can be funny, but should remain useful and context-aware.

## Nightly Flow

Yappl is meant to be used before bed.

After a certain evening time, if the user has not completed a yap session for the day, the device should gently remind them with:

- A slowly breathing light.
- OLED text indicating that they have not journaled yet.

If the room lights are off and the user still has not yapped, the device may flash the light as a stronger reminder.

If the user has already completed a session, the device should stay idle and may show a calm visual on the OLED.

## Session Controls

The user starts a session with one large button.

The user also stops a session with the button. The session is not fixed to exactly five minutes; the user decides when to stop.

There should also be a hidden on/off switch.

## During a Yap Session

During a session, Yappl should:

- Listen to the user.
- Stream or upload audio to a cloud backend.
- Show a clear recording state and elapsed time.
- Avoid interrupting the user.

## Hardware Direction

Current hardware:

- ESP32-S3.
- 128x128 SH1107 OLED.
- INMP441 I2S microphone.

Planned or desired hardware:

- One large user-facing button.
- Hidden on/off switch.
- Status LED or breathing light.

The current 128x128 OLED is enough for now.

The device will sit on a bedside table and should be small, plugged in, and feel like a desk object rather than a generic gadget.

## Audio Behavior

Audio quality does not need to be studio-grade. It only needs to be understandable enough for transcription.

The device should detect silence and may display text during silence.

The cloud should store:

- Raw audio.
- Text transcription.
- AI summary.

Raw audio should not be deleted after transcription, though it may be compressed.

## AI Behavior

The AI should:

- Transcribe the completed session.
- Produce a useful daily summary afterward.
- Identify key events and notable details.
- Detect moods and themes.
- Remember previous entries.
- Surface patterns over time in summaries and progress views.

The AI should stay grounded in what the user actually recorded and avoid inventing details.

## Data and Privacy

All journal data should live in a cloud application built for Yappl.

Stored data should include:

- Audio.
- Raw transcripts.
- Summaries.
- Possibly metadata such as dates, moods, themes, and completion streaks.

Raw transcripts do not need a formal edit flow. They may be stored as text files or text records.

Exports to Markdown, PDF, or JSON may be useful later.

Encryption is not required for the personal MVP.

The system only needs to support one user for now.

## Connectivity

The device is expected to always have Wi-Fi because it lives in the user's room.

If Wi-Fi is down at bedtime, the device should not try to work offline. It should tell the user to connect to Wi-Fi or come back later.

Offline recording and later upload are not required.

Wi-Fi provisioning is undecided. Options include hardcoded Wi-Fi for early prototypes, captive portal setup, or phone-based setup later.

## Power and Form

The device will be plugged in.

Battery life is not a concern for the current direction.

The physical form should be small and feel like a desk object.

## MVP Scope

The simplest version worth using for one week:

- Records the user's voice.
- Produces transcripts.
- Reminds the user when they have not recorded anything that day.

Features that can wait:

- Phone app.
- Interface for browsing raw data and summaries.
- Rich progress tracking.

The session should not be limited to 30 seconds. The user decides when to stop.

## Backend Direction

A backend/server will be needed.

The backend should eventually handle:

- Device authentication.
- Audio upload or streaming.
- Transcription.
- AI summary generation.
- Journal storage.
- Future app/API access.

The backend should keep API keys and sensitive cloud credentials off the ESP32 device.

## Suggested Milestone Order

1. Make the normal OLED mic meter reliable.
2. Add the button and session state machine.
3. Add Wi-Fi connection and clear Wi-Fi failure UI.
4. Add a simple backend endpoint.
5. Upload short audio or test data from the device.
6. Add transcription on the backend.
7. Store transcript and audio in the cloud.
8. Add nightly reminder logic with LED and OLED.
9. Add summaries after each session.
