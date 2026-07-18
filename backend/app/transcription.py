import subprocess
from pathlib import Path

from .settings import settings


def transcript_path(session_folder: Path) -> Path:
    return session_folder / "transcript.txt"


def transcribe_mp3(session_folder: Path) -> dict:
    """Transcribe one completed session locally with whisper.cpp."""
    source = session_folder / "audio.mp3"
    wav = session_folder / "audio.transcription.wav"
    output = transcript_path(session_folder)

    if not settings.yappl_transcription_enabled:
        return {"transcription_status": "disabled"}
    if not source.exists() or source.stat().st_size == 0:
        return {
            "transcription_status": "skipped",
            "transcription_error": "MP3 is missing or empty",
        }

    try:
        subprocess.run(
            [
                "ffmpeg",
                "-y",
                "-i",
                str(source),
                "-ar",
                "16000",
                "-ac",
                "1",
                "-c:a",
                "pcm_s16le",
                str(wav),
            ],
            capture_output=True,
            text=True,
            check=True,
            timeout=settings.yappl_transcription_timeout_seconds,
        )

        # -of is the output prefix; whisper.cpp adds the .txt suffix.
        subprocess.run(
            [
                settings.yappl_whisper_cli,
                "-m",
                settings.yappl_whisper_model,
                "-f",
                str(wav),
                "-l",
                settings.yappl_whisper_language,
                "-otxt",
                "-of",
                str(output.with_suffix("")),
                "-np",
            ],
            capture_output=True,
            text=True,
            check=True,
            timeout=settings.yappl_transcription_timeout_seconds,
        )

        if not output.exists():
            raise RuntimeError("whisper.cpp completed without creating transcript.txt")

        text = output.read_text().strip()
        output.write_text(f"{text}\n" if text else "")
        return {
            "transcription_status": "complete",
            "transcript_file": str(output),
            "transcript_bytes": output.stat().st_size,
            "transcription_model": Path(settings.yappl_whisper_model).name,
        }
    except subprocess.TimeoutExpired:
        return {
            "transcription_status": "failed",
            "transcription_error": "local transcription timed out",
        }
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        detail = str(error)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            detail = error.stderr[-2000:]
        return {
            "transcription_status": "failed",
            "transcription_error": detail,
        }
    finally:
        wav.unlink(missing_ok=True)
