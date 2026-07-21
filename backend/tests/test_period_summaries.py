import tempfile
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import patch
from zoneinfo import ZoneInfo

from app.period_summaries import ensure_period_summaries, journal_date, summary_target_words
from app.settings import settings


class PeriodSummaryTest(unittest.TestCase):
    def test_summary_lengths_scale_by_period(self) -> None:
        source = "word " * 10000
        self.assertEqual(summary_target_words("weekly", source), 300)
        self.assertEqual(summary_target_words("monthly", source), 600)
        self.assertEqual(summary_target_words("yearly", source), 1200)
        self.assertEqual(summary_target_words("weekly", "brief"), 120)
        self.assertEqual(summary_target_words("monthly", "brief"), 300)
        self.assertEqual(summary_target_words("yearly", "brief"), 700)

    def test_eight_am_boundary_and_cached_weekly_summary(self) -> None:
        zone = ZoneInfo("America/Los_Angeles")
        early_monday = int(datetime(2025, 1, 13, 7, 0, tzinfo=zone).timestamp())
        self.assertEqual(journal_date(early_monday).isoformat(), "2025-01-12")
        sessions = [
            {
                "session_id": "session_one",
                "completed_at_epoch": early_monday,
                "summary": "I finished a project and had dinner with friends.",
            }
        ]
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.period_summaries.generate_text", return_value=("A reflective week.", "test", "model")) as generate,
            ):
                first = ensure_period_summaries("weekly", sessions)
                second = ensure_period_summaries("weekly", sessions)
            self.assertEqual(first[0]["period_start"], "2025-01-06")
            self.assertEqual(first[0]["period_end"], "2025-01-12")
            self.assertEqual(second[0]["summary"], "A reflective week.")
            self.assertEqual(generate.call_count, 1)
            self.assertTrue((Path(temporary_directory) / "summaries" / "weekly" / "2025-01-06.md").exists())

    def test_completed_calendar_year_is_supported(self) -> None:
        zone = ZoneInfo("America/Los_Angeles")
        sessions = [{"session_id": "session_year", "completed_at_epoch": int(datetime(2025, 6, 2, 20, tzinfo=zone).timestamp()), "summary": "A meaningful day."}]
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                patch.object(settings, "yappl_storage_dir", temporary_directory),
                patch("app.period_summaries.generate_text", return_value=("A meaningful year.", "test", "model")),
            ):
                result = ensure_period_summaries("yearly", sessions)
            self.assertEqual(result[0]["period_start"], "2025-01-01")
            self.assertEqual(result[0]["period_end"], "2025-12-31")
            self.assertTrue((Path(temporary_directory) / "summaries" / "yearly" / "2025-01-01.md").exists())


if __name__ == "__main__":
    unittest.main()
