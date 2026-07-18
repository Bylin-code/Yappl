# Yappl Backend

## Deploy locally

Requirements: Docker Desktop.

```bash
cd backend
cp .env.example .env
docker compose up -d --build
```

The first build compiles `whisper.cpp` and downloads the local English transcription model.

Verify:

```bash
curl http://localhost:8000/health
docker compose ps
```

View logs:

```bash
docker compose logs -f api
```

Stop:

```bash
docker compose down
```

The ESP32 must use the host computer's LAN address, for example:

```text
http://10.0.0.144:8000
```
