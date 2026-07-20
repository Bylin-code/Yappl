import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from app.memory import correct_transcript, learn_from_session, list_entities, relevant_memory_context
from app.settings import settings


class MemoryTest(unittest.TestCase):
    def test_seeds_ylang_and_corrects_known_alias_without_losing_raw_text(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            folder = Path(temporary_directory) / "session_test"
            folder.mkdir()
            (folder / "transcript.txt").write_text("I called Ylah and worked on Yapple.")

            with patch.object(settings, "yappl_storage_dir", temporary_directory):
                result = correct_transcript("session_test", folder)
                entities = list_entities()
                context = relevant_memory_context((folder / "transcript.txt").read_text())

            self.assertEqual((folder / "transcript.raw.txt").read_text(), "I called Ylah and worked on Yapple.\n")
            self.assertEqual((folder / "transcript.corrected.txt").read_text(), "I called Ylang and worked on Yappl.\n")
            self.assertEqual((folder / "transcript.txt").read_text(), "I called Ylang and worked on Yappl.\n")
            self.assertEqual(len(result["transcript_corrections"]), 2)
            self.assertTrue(any(entity["canonical_name"] == "Ylang" for entity in entities))
            self.assertIn("Brady's girlfriend", context)

    def test_unique_local_name_variation_is_learned_and_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            first = Path(temporary_directory) / "session_first"
            first.mkdir()
            (first / "transcript.txt").write_text("I called Yleng after work.")

            with patch.object(settings, "yappl_storage_dir", temporary_directory):
                result = correct_transcript("session_first", first)
                entities = list_entities()

            self.assertEqual((first / "transcript.txt").read_text(), "I called Ylang after work.\n")
            self.assertEqual(result["memory_local_aliases_learned"][0]["alias"], "Yleng")
            ylang = next(entity for entity in entities if entity["canonical_name"] == "Ylang")
            self.assertTrue(any(alias["alias"] == "Yleng" and alias["source"].startswith("local:") for alias in ylang["aliases"]))

    def test_existing_memory_call_can_add_strong_contextual_alias(self) -> None:
        response = '{"facts":[],"aliases":[{"entity_id":"person_ylang","alias":"Eelang","confidence":0.96}],"new_entities":[]}'
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.summarization.generate_text", return_value=(response, "test", "test-model")),
            ):
                result = learn_from_session("session_ai", "I had dinner with Eelang.")
                entities = list_entities()

            self.assertEqual(result["memory_ai_aliases_learned"][0]["alias"], "Eelang")
            ylang = next(entity for entity in entities if entity["canonical_name"] == "Ylang")
            self.assertTrue(any(alias["alias"] == "Eelang" and alias["source"] == "ai:session_ai" for alias in ylang["aliases"]))

    def test_ai_alias_is_rejected_when_not_present_or_low_confidence(self) -> None:
        response = '{"facts":[],"aliases":[{"entity_id":"person_ylang","alias":"Imaginary","confidence":0.99},{"entity_id":"person_ylang","alias":"Eelang","confidence":0.7}],"new_entities":[]}'
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.summarization.generate_text", return_value=(response, "test", "test-model")),
            ):
                result = learn_from_session("session_rejected", "I had dinner with Eelang.")

            self.assertEqual(result["memory_ai_aliases_learned"], [])


if __name__ == "__main__":
    unittest.main()
