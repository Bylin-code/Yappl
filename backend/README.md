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

