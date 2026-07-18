import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from app.transcription import transcribe_mp3


class TranscriptionTest(unittest.TestCase):
    def test_writes_transcript_and_removes_temporary_wav(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            session_folder = Path(temporary_directory)
            (session_folder / "audio.mp3").write_bytes(b"fake mp3")

            def fake_run(command, **_kwargs):
                if command[0] == "ffmpeg":
                    Path(command[-1]).write_bytes(b"fake wav")
                else:
                    output_prefix = Path(command[command.index("-of") + 1])
                    output_prefix.with_suffix(".txt").write_text("hello from yappl")

            with patch("app.transcription.subprocess.run", side_effect=fake_run):
                result = transcribe_mp3(session_folder)

            self.assertEqual(result["transcription_status"], "complete")
            self.assertEqual((session_folder / "transcript.txt").read_text(), "hello from yappl\n")
            self.assertFalse((session_folder / "audio.transcription.wav").exists())


if __name__ == "__main__":
    unittest.main()
