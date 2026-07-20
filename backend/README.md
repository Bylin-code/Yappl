# Yappl Backend

## Deploy locally

Requirements: Docker Desktop.

```bash
cd backend
cp .env.example .env
# Edit .env and paste the Anthropic key after ANTHROPIC_API_KEY=
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

Each completed session is transcribed locally and summarized using the selected
provider. The default is Anthropic `claude-sonnet-5`. Summaries are saved as
`summary.txt` beside that session's audio and transcript. Provider selection and
encrypted key storage are available through `GET/PUT /settings/summary`.

Personal memory is stored locally in `/data/memory.sqlite3`. Yappl preserves
`transcript.raw.txt`, writes name corrections to `transcript.corrected.txt`, and
uses relevant people/project facts as summary context. Manage memory through
`GET /memory/entities` and `PUT /memory/entities/{entity_id}`.

The ESP32 must use the host computer's LAN address, for example:

```text
http://10.0.0.144:8000
```
