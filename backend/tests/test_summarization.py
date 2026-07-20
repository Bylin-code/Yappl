import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from app.summarization import summary_target_words, summarize_transcript, system_prompt_for


class SummarizationTest(unittest.TestCase):
    def test_summary_length_scales_with_transcript(self) -> None:
        self.assertEqual(summary_target_words("word " * 130), 75)
        self.assertEqual(summary_target_words("word " * 650), 125)
        self.assertEqual(summary_target_words("word " * 2600), 475)
        self.assertEqual(summary_target_words("word " * 10000), 600)
        self.assertIn("approximately 2600 words", system_prompt_for("word " * 2600))
        self.assertIn("retaining proportionally more concrete detail", system_prompt_for("word " * 2600))
        self.assertIn("natural first-person journal entry", system_prompt_for("word " * 2600))
        self.assertIn("natural shifts in time", system_prompt_for("word " * 2600))

    def test_openai_compatible_summary_is_saved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            folder = Path(temporary_directory)
            (folder / "transcript.txt").write_text("Today went well.")
            response = Mock()
            response.raise_for_status.return_value = None
            response.json.return_value = {"choices": [{"message": {"content": "Summary\nA good day."}}]}
            config = {"provider": "openai", "model": "gpt-5-nano"}

            with (
                patch("app.summarization.load_provider_config", return_value=config),
                patch("app.summarization.provider_api_key", return_value="secret"),
                patch("app.summarization.httpx.post", return_value=response),
            ):
                result = summarize_transcript(folder)

            self.assertEqual(result["summary_status"], "complete")
            self.assertEqual((folder / "summary.txt").read_text(), "Summary\nA good day.\n")
            self.assertNotIn("secret", str(result))


if __name__ == "__main__":
    unittest.main()
