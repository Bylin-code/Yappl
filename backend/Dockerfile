FROM debian:bookworm-slim AS whisper-builder

ARG WHISPER_CPP_VERSION=v1.8.1
ARG WHISPER_MODEL=base.en

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates cmake curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN curl -fsSL "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${WHISPER_CPP_VERSION}.tar.gz" \
    | tar -xz --strip-components=1 \
    && cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF \
    && cmake --build build --target whisper-cli --config Release -j 2 \
    && ./models/download-ggml-model.sh "${WHISPER_MODEL}"

FROM python:3.12-slim

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=whisper-builder /src/build/bin/whisper-cli /usr/local/bin/whisper-cli
COPY --from=whisper-builder /src/models/ggml-base.en.bin /opt/whisper/models/ggml-base.en.bin

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app
COPY tests ./tests

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
