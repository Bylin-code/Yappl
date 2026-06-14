# Yappl Backend and Docker Notes

## Purpose

Yappl will eventually need a backend so the physical device can send journal data somewhere permanent.

The backend is the cloud application that would receive data from the ESP32, store it, process it with AI, and make it available to a future phone app.

At a high level:

```text
Yappl device
  records audio / sends events
        |
        v
Backend API
  receives requests from the device
  stores data in a database
  talks to AI services
        |
        v
Phone app / web app
  shows summaries, transcripts, recordings, and progress
```

The firmware should not directly own all product data forever. The device should mostly collect data, show local status, and communicate with the backend.

## What the Backend Is

The backend is a program running on a computer or cloud server.

For Yappl, it would likely expose HTTP API routes such as:

```text
GET  /health
POST /device/session/start
POST /device/session/audio
POST /device/session/finish
GET  /entries
GET  /entries/{entry_id}
```

The ESP32 would call these routes over Wi-Fi.

Example:

```text
ESP32 -> POST /device/session/finish -> Backend
```

The backend would then save the session data and return a response.

## How HTTP Works

HTTP is the request/response protocol used by browsers, phone apps, servers, and the ESP32 when it talks to a web API.

The basic idea is:

```text
Client sends a request
        |
        v
Server receives it and does work
        |
        v
Server sends back a response
```

For Yappl:

```text
ESP32 device = HTTP client
Yappl backend = HTTP server
Phone app = HTTP client
```

The ESP32 does not "open" the backend directly like a person opens a website. It sends structured HTTP requests to API routes.

## HTTP Requests

An HTTP request has a few important parts:

```text
method
url
headers
body
```

Example Yappl request:

```http
POST /device/ping HTTP/1.1
Host: 192.168.1.25:8000
Content-Type: application/json
Authorization: Bearer local_device_secret

{
  "device_id": "yappl_dev_001",
  "wifi_connected": true,
  "time_synced": true
}
```

What each part means:

```text
POST
```

The method. `POST` usually means "send data to the backend."

```text
/device/ping
```

The route. This tells the backend which function should handle the request.

```text
Host: 192.168.1.25:8000
```

The server address. During local testing this may be your Mac's LAN IP address.

```text
Content-Type: application/json
```

This tells the backend that the request body is JSON.

```text
Authorization: Bearer local_device_secret
```

This is how the device can prove it is allowed to call the backend.

```json
{
  "device_id": "yappl_dev_001",
  "wifi_connected": true,
  "time_synced": true
}
```

This is the request body. It contains the actual data being sent.

## HTTP Responses

After the backend receives a request, it sends back an HTTP response.

Example response:

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "status": "ok",
  "server_time": "2026-06-14T00:15:00-07:00"
}
```

The important parts are:

```text
status code
headers
body
```

```text
200 OK
```

The request worked.

```json
{
  "status": "ok",
  "server_time": "2026-06-14T00:15:00-07:00"
}
```

The backend returned JSON data that the device can read.

## HTTP Methods

HTTP methods describe what kind of action the client wants.

For Yappl, the most common methods will be:

```text
GET
  Read something from the backend.

POST
  Send new data to the backend.

PATCH
  Update part of something that already exists.

DELETE
  Delete something.
```

Examples:

```text
GET /health
  Ask if the backend is alive.

POST /device/ping
  Device checks in with the backend.

POST /device/session/start
  Device says a yap session started.

POST /device/session/audio
  Device uploads audio data.

POST /device/session/finish
  Device says a yap session ended.

GET /entries
  Phone app asks for journal entries.

GET /entries/{entry_id}
  Phone app asks for one specific journal entry.
```

## HTTP Status Codes

Status codes tell the client what happened.

Important ones for Yappl:

```text
200 OK
  The request worked.

201 Created
  Something new was created, like a new journal session.

400 Bad Request
  The device or app sent invalid data.

401 Unauthorized
  The device or app did not provide a valid secret/token.

404 Not Found
  The requested route or entry does not exist.

500 Internal Server Error
  The backend crashed or hit an unexpected bug.
```

The ESP32 firmware can use these codes to decide what to show on the OLED.

Example:

```text
200
  show normal connected state

401
  show backend auth error

500
  show backend error / try again later
```

## JSON

JSON is the main data format the backend, ESP32, and phone app should use for normal API calls.

Example JSON:

```json
{
  "device_id": "yappl_dev_001",
  "session_id": "session_123",
  "completed_at": "2026-06-14T00:20:00-07:00"
}
```

JSON is useful because:

```text
It is easy for humans to read.
It is easy for backend code to parse.
It is easy for phone apps to parse.
The ESP32 can generate it for small messages.
```

For large raw audio uploads, we may not use JSON for the audio bytes themselves. Audio may eventually be uploaded as binary data or chunked files.

## HTTP in Yappl's Local Setup

During local testing, the backend runs on your Mac.

Example:

```text
Backend URL on Mac:
http://localhost:8000
```

Your browser can use that URL because your browser is running on the same Mac.

The ESP32 cannot use that URL.

For the ESP32, `localhost` means:

```text
the ESP32 itself
```

So the ESP32 needs your Mac's Wi-Fi IP address instead.

Example:

```text
Backend URL from ESP32:
http://192.168.1.25:8000
```

Then the flow is:

```text
ESP32
  sends HTTP request over Wi-Fi
        |
        v
Router
        |
        v
Mac running backend
        |
        v
Backend sends HTTP response back
```

## HTTP in Yappl's Hosted Setup

Once the backend is hosted, the ESP32 and phone app use a public URL.

Example:

```text
https://api.yappl.example.com
```

Then the flow is:

```text
ESP32
        |
        v
Internet
        |
        v
Hosted Yappl backend
        |
        v
Database / storage / AI services
```

The phone app would use the same backend:

```text
Phone app
        |
        v
https://api.yappl.example.com
```

This is why the backend becomes the center of the product. The device and phone app are both clients.

## Example Yappl Session Flow Over HTTP

A simple future session could look like this:

```text
1. Device starts a session
   POST /device/session/start

2. Backend creates a session record
   returns session_id

3. Device uploads audio chunks
   POST /device/session/audio

4. Device finishes the session
   POST /device/session/finish

5. Backend transcribes and summarizes
   stores transcript and summary

6. Phone app loads the result
   GET /entries/{entry_id}
```

Example start request:

```http
POST /device/session/start HTTP/1.1
Content-Type: application/json
Authorization: Bearer local_device_secret

{
  "device_id": "yappl_dev_001",
  "started_at": "2026-06-14T00:15:00-07:00"
}
```

Example response:

```json
{
  "session_id": "session_123",
  "status": "recording"
}
```

The ESP32 would save `session_id` temporarily so future audio chunks are attached to the right journal session.

## HTTP vs Web Hosting

HTTP is the communication protocol.

Web hosting is where the backend runs.

They are related but different:

```text
HTTP
  The language clients and servers use to talk.

Hosting
  The place where the server is running.
```

Docker helps package the backend.

Hosting runs that packaged backend somewhere reachable.

HTTP is how the ESP32 and phone app communicate with it.

## Why Use Docker

Docker lets the backend run inside a controlled environment called a container.

Without Docker, the backend depends on whatever is installed directly on your Mac:

```text
Your Mac
  Python version
  installed packages
  database setup
  environment variables
```

That can become messy because different computers and servers may have different versions installed.

With Docker, the backend describes its own environment:

```text
Backend container
  specific Python version
  specific dependencies
  specific startup command
  specific exposed port
```

This means the backend can run almost the same way on your Mac now and on a cloud server later.

## Docker Mental Model

Your Mac is the host machine.

Docker runs isolated containers on top of it.

For Yappl, local development might look like this:

```text
Mac
  Docker
    api container
      runs the Yappl backend

    database container
      runs Postgres
```

The backend and database are separate containers.

They can talk to each other inside Docker, but your browser, phone app, or ESP32 talks to the backend through a port.

Example:

```text
http://localhost:8000
```

That means:

```text
localhost = your Mac
8000      = the backend API port
```

## Dockerfile

A `Dockerfile` explains how to build the backend container.

Example:

```dockerfile
FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install -r requirements.txt

COPY app ./app

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

What each part means:

```text
FROM python:3.12-slim
```

Start from a small Linux image that already has Python 3.12 installed.

```text
WORKDIR /app
```

Use `/app` as the main folder inside the container.

```text
COPY requirements.txt .
```

Copy the dependency list into the container.

```text
RUN pip install -r requirements.txt
```

Install the Python packages the backend needs.

```text
COPY app ./app
```

Copy the backend source code into the container.

```text
CMD [...]
```

Start the backend server when the container runs.

## Docker Compose

Docker Compose lets you run multiple containers together with one command.

For Yappl, the two main containers would be:

```text
api
  the backend server

db
  the Postgres database
```

Example `docker-compose.yml`:

```yaml
services:
  api:
    build: .
    ports:
      - "8000:8000"
    env_file:
      - .env

  db:
    image: postgres:16
    environment:
      POSTGRES_USER: yappl
      POSTGRES_PASSWORD: yappl_password
      POSTGRES_DB: yappl
```

Then you would run:

```bash
docker compose up --build
```

That command:

1. Builds the backend image.
2. Starts the backend container.
3. Starts the database container.
4. Shows logs in the terminal.

## Local Development Flow

Once backend code exists, the normal local flow would be:

```bash
cd backend
cp .env.example .env
docker compose up --build
```

Then test the backend:

```bash
curl http://localhost:8000/health
```

Expected response:

```json
{
  "status": "ok"
}
```

If that works, the backend is running.

## How the ESP32 Reaches the Backend

If the backend is running on your Mac, the ESP32 cannot use:

```text
http://localhost:8000
```

On the ESP32, `localhost` means the ESP32 itself, not your Mac.

For local testing, the ESP32 needs your Mac's local network IP address.

Example:

```text
http://192.168.1.25:8000
```

Then the flow is:

```text
ESP32
  connected to Wi-Fi
        |
        v
Mac on same Wi-Fi
  running backend at 192.168.1.25:8000
```

Later, when the backend is hosted, the ESP32 would call a real URL:

```text
https://api.yappl.example.com
```

## How This Transfers to Cloud Hosting

Docker is useful because many hosting platforms can run the same container.

The path can look like this:

```text
Local development
  docker compose on your Mac

Early hosted version
  Render / Fly.io / Railway / similar platform

Later production version
  AWS ECS / AWS Fargate / Elastic Beanstalk / similar AWS service
```

The backend code can stay mostly the same.

The main things that change are:

```text
Where the container runs
Where the database lives
What environment variables are configured
What public URL points to the backend
```

## Environment Variables

Backend settings should not be hardcoded directly into source files.

They should live in environment variables.

Examples:

```text
DATABASE_URL
OPENAI_API_KEY
YAPPL_DEVICE_SECRET
STORAGE_BUCKET
```

Local development usually uses a `.env` file:

```text
DATABASE_URL=postgresql://yappl:yappl_password@db:5432/yappl
YAPPL_DEVICE_SECRET=local_dev_secret
```

The `.env` file should not be committed to git if it contains real secrets.

Instead, commit an example file:

```text
.env.example
```

The real hosted backend would have these values configured in the hosting provider's dashboard.

## Database Role

The database stores structured information.

For Yappl, the database could store:

```text
users
devices
journal_sessions
transcripts
summaries
mood_tags
reminder_state
```

The database should not usually store large raw audio files directly.

A better structure is:

```text
Database
  session metadata
  transcript text
  summary text
  audio file URL

Object storage
  raw audio file
```

Object storage means something like S3 later.

## Audio Storage Role

Raw recordings can get large.

Instead of putting audio directly into Postgres, the backend should eventually upload recordings to file storage.

Possible future flow:

```text
ESP32 uploads audio
        |
        v
Backend receives audio
        |
        v
Backend stores file in object storage
        |
        v
Backend stores file URL in database
```

This keeps the database fast and manageable.

## AI Processing Role

The backend should own AI calls, not the ESP32.

Reason:

```text
ESP32
  limited memory
  limited processing power
  should not store API keys

Backend
  can safely store API keys
  can call AI APIs
  can retry failed requests
  can save generated summaries
```

The likely flow is:

```text
ESP32 records/sends audio
        |
        v
Backend transcribes audio
        |
        v
Backend summarizes journal entry
        |
        v
Backend saves transcript and summary
        |
        v
Phone app shows the result
```

Live AI follow-up questions can come later after basic upload and storage work.

## Future Phone App Role

The phone app should not talk directly to the ESP32 for journal history.

It should talk to the backend.

```text
Phone app
        |
        v
Backend
        |
        v
Database and audio storage
```

That lets the phone app show:

```text
daily summaries
raw transcripts
recordings
streaks
mood trends
progress over time
```

## Recommended First Backend Milestone

The first useful backend should be very small.

Start with:

```text
GET /health
  Confirms the backend is running.

POST /device/ping
  Confirms the ESP32 can reach the backend.

POST /device/session-completed
  Lets the ESP32 tell the backend that a yap session happened.
```

This would prove:

```text
ESP32 Wi-Fi works
ESP32 can call the backend
Backend can receive device events
Backend can store basic state
```

Only after that should audio upload, transcription, summaries, and phone app views be added.
