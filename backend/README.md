# Yappl Backend

Local FastAPI backend for proving that the Yappl firmware can reach a server.

## Run

```bash
cp .env.example .env
docker compose up --build
```

## Test

```bash
curl http://localhost:8000/health
```

Expected:

```json
{"status":"ok"}
```

The ESP32 must use your computer's LAN IP instead of `localhost`.

Example:

```text
http://192.168.1.25:8000
```

## Audio Storage

Docker Compose mounts local durable storage here:

```text
backend/data/
```

Each recorded session is saved as:

```text
backend/data/devices/<device_id>/state.json
backend/data/devices/<device_id>/sessions/<session_id>/metadata.json
backend/data/devices/<device_id>/sessions/<session_id>/audio.pcm_s16le
backend/data/devices/<device_id>/sessions/<session_id>/audio.mp3
```

Older local recordings in `backend/data/sessions/<session_id>/` are still read
for compatibility, but new recordings are written under the device folder.

The audio file is raw mono signed 16-bit little-endian PCM at the sample rate in
`metadata.json`. When the session finishes, the backend also converts that PCM
file into an MP3 using FFmpeg. The MP3 bitrate is configured by
`YAPPL_MP3_BITRATE`.

Download metadata:

```bash
curl "http://localhost:8000/device/session/<session_id>" \
  -H "Authorization: Bearer local_dev_secret"
```

Download raw audio:

```bash
curl "http://localhost:8000/device/session/<session_id>/audio" \
  -H "Authorization: Bearer local_dev_secret" \
  --output audio.pcm_s16le
```

Download MP3:

```bash
curl "http://localhost:8000/device/session/<session_id>/audio.mp3" \
  -H "Authorization: Bearer local_dev_secret" \
  --output audio.mp3
```
