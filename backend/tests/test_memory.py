import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from app.memory import correct_transcript, list_entities, relevant_memory_context
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


if __name__ == "__main__":
    unittest.main()
