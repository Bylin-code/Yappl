import tempfile
import unittest
from unittest.mock import patch

from fastapi.testclient import TestClient

from app.main import app, summary_preview
from app.settings import settings


class SessionApiTest(unittest.TestCase):
    def test_summary_preview_returns_one_or_two_clean_sentences(self) -> None:
        summary = "The morning was spent testing Wally for a customer demonstration. Lunch was with the intern team. The evening was quiet."
        self.assertEqual(
            summary_preview(summary),
            "The morning was spent testing Wally for a customer demonstration. Lunch was with the intern team.",
        )

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.storage_patch = patch.object(settings, "yappl_storage_dir", self.temporary_directory.name)
        self.storage_patch.start()
        self.client = TestClient(app)
        self.client.__enter__()
        self.headers = {"Authorization": f"Bearer {settings.yappl_device_secret}"}

    def tearDown(self) -> None:
        self.client.__exit__(None, None, None)
        self.storage_patch.stop()
        self.temporary_directory.cleanup()

    def start_session(self) -> str:
        response = self.client.post(
            "/device/session/start",
            headers=self.headers,
            json={"device_id": "test_device", "sample_rate_hz": 16000},
        )
        self.assertEqual(response.status_code, 200, response.text)
        return response.json()["session_id"]

    def upload(self, session_id: str, sequence: int, body: bytes):
        return self.client.post(
            "/device/session/audio",
            params={"session_id": session_id, "sequence": sequence},
            headers={**self.headers, "Content-Type": "application/octet-stream"},
            content=body,
        )

    def test_duplicate_chunk_is_acknowledged_without_duplicate_audio(self) -> None:
        session_id = self.start_session()
        first = self.upload(session_id, 1, b"audio-one")
        duplicate = self.upload(session_id, 1, b"audio-one")

        self.assertEqual(first.status_code, 200, first.text)
        self.assertEqual(duplicate.status_code, 200, duplicate.text)
        self.assertTrue(duplicate.json()["duplicate"])
        self.assertEqual(duplicate.json()["audio_bytes"], len(b"audio-one"))

    def test_out_of_order_and_conflicting_chunks_are_rejected(self) -> None:
        session_id = self.start_session()
        out_of_order = self.upload(session_id, 2, b"second")
        self.assertEqual(out_of_order.status_code, 409, out_of_order.text)

        self.assertEqual(self.upload(session_id, 1, b"first").status_code, 200)
        conflict = self.upload(session_id, 1, b"different")
        self.assertEqual(conflict.status_code, 409, conflict.text)

    def test_chunk_and_session_size_limits_are_enforced(self) -> None:
        session_id = self.start_session()
        with patch.object(settings, "yappl_audio_chunk_max_bytes", 4):
            response = self.upload(session_id, 1, b"12345")
        self.assertEqual(response.status_code, 413, response.text)

        with patch.object(settings, "yappl_session_max_bytes", 4):
            response = self.upload(session_id, 1, b"1234")
            self.assertEqual(response.status_code, 200, response.text)
            response = self.upload(session_id, 2, b"5")
        self.assertEqual(response.status_code, 413, response.text)

    def test_audio_is_rejected_after_session_finish(self) -> None:
        session_id = self.start_session()
        self.assertEqual(self.upload(session_id, 1, b"audio").status_code, 200)

        def skip_mp3(_session_id, metadata):
            metadata["mp3_status"] = "skipped_for_test"
            return metadata

        with patch("app.main.convert_pcm_to_mp3", side_effect=skip_mp3):
            response = self.client.post(
                "/device/session/finish",
                headers=self.headers,
                json={"device_id": "test_device", "session_id": session_id, "completed_at_epoch": 1234},
            )
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(self.upload(session_id, 2, b"late").status_code, 409)


if __name__ == "__main__":
    unittest.main()
