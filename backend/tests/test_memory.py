import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from app.memory import canonicalize_known_aliases, correct_transcript, delete_entity, learn_from_session, list_entities, list_pending_entities, promote_pending_entity, relevant_memory_context, upsert_entity
from app.settings import settings


class MemoryTest(unittest.TestCase):
    def test_user_can_replace_and_delete_memory_without_seed_resurrection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with patch.object(settings, "yappl_storage_dir", temporary_directory):
                entity = upsert_entity(None, "object", "Blue Car", "My daily car.", ["Car"], [{"predicate": "color", "value": "blue"}])
                updated = upsert_entity(entity["id"], "object", "Green Car", "My updated car.", [], [{"predicate": "color", "value": "green"}], replace=True)
                self.assertEqual([alias["alias"] for alias in updated["aliases"]], ["Blue Car", "Green Car"])
                self.assertEqual(canonicalize_known_aliases("I drove my Blue Car."), "I drove my Green Car.")
                self.assertEqual([(fact["predicate"], fact["value"]) for fact in updated["facts"]], [("color", "green")])
                self.assertFalse((Path(temporary_directory) / "memory" / "objects" / "blue-car.md").exists())
                self.assertTrue(delete_entity(entity["id"]))
                self.assertFalse(any(item["id"] == entity["id"] for item in list_entities()))
                self.assertTrue(delete_entity("person_ylang"))
                self.assertFalse(any(item["id"] == "person_ylang" for item in list_entities()))

    def test_saved_text_uses_current_canonical_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with patch.object(settings, "yappl_storage_dir", temporary_directory):
                corrected = canonicalize_known_aliases("I called Ylah after working on Yapple.")
            self.assertEqual(corrected, "I called Ylang after working on Yappl.")

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

    def test_recurring_entities_promote_and_readable_directory_is_generated(self) -> None:
        response = """{
          "facts": [], "aliases": [],
          "new_entities": [{
            "type": "place", "canonical_name": "Golden Gate Park",
            "description": "A park the user visits regularly.", "aliases": ["the park"],
            "facts": [{"predicate": "activity", "value": "weekend walks"}]
          }]
        }"""
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.summarization.generate_text", return_value=(response, "test", "test-model")),
            ):
                first = learn_from_session("session_one", "I walked through Golden Gate Park again.")
                second = learn_from_session("session_two", "I returned to Golden Gate Park this weekend.")

            root = Path(temporary_directory) / "memory"
            self.assertEqual(first["memory_entities_learned"], [])
            self.assertEqual(first["memory_entities_staged"][0]["canonical_name"], "Golden Gate Park")
            self.assertEqual(second["memory_entities_learned"][0]["canonical_name"], "Golden Gate Park")
            self.assertIn("weekend walks", (root / "places" / "golden-gate-park.md").read_text())
            self.assertTrue((root / "people").is_dir())
            self.assertTrue((root / "objects").is_dir())
            self.assertTrue((root / "events").is_dir())

    def test_user_can_review_and_promote_pending_memory(self) -> None:
        response = '{"facts":[],"aliases":[],"new_entities":[{"type":"place","canonical_name":"Ocean Beach","description":"A recurring beach.","aliases":["the beach"],"facts":[{"predicate":"activity","value":"walks"}]}]}'
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.summarization.generate_text", return_value=(response, "test", "test-model")),
            ):
                learn_from_session("session_one", "I returned to Ocean Beach.")
                pending = list_pending_entities()[0]
                entity = promote_pending_entity(
                    pending["normalized_key"], "event", "Ocean Walks", "Our recurring walks.", [], [{"predicate": "with", "value": "friends"}]
                )
                self.assertEqual(entity["type"], "event")
                self.assertEqual(list_pending_entities(), [])
                self.assertTrue((Path(temporary_directory) / "memory" / "events" / "ocean-walks.md").exists())

    def test_learning_receives_full_snapshot_and_can_replace_stale_fact(self) -> None:
        response = """{
          "facts": [{"entity_id":"person_ylang","predicate":"relationship_to_user","value":"partner","replace_existing":true}],
          "aliases": [], "new_entities": []
        }"""
        captured = {}

        def fake_generate(system_prompt, transcript, max_tokens):
            captured["system_prompt"] = system_prompt
            captured["transcript"] = transcript
            captured["max_tokens"] = max_tokens
            return response, "test", "test-model"

        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.summarization.generate_text", side_effect=fake_generate),
            ):
                learn_from_session("session_update", "Ylang is now my partner.")
                entities = list_entities()

            self.assertIn('"pending_entities":[]', captured["system_prompt"])
            self.assertIn('"predicate":"relationship_to_user"', captured["system_prompt"])
            ylang = next(entity for entity in entities if entity["canonical_name"] == "Ylang")
            relationship_values = [fact["value"] for fact in ylang["facts"] if fact["predicate"] == "relationship_to_user"]
            self.assertEqual(relationship_values, ["partner"])


if __name__ == "__main__":
    unittest.main()
