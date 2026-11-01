import json
import unittest
from tempfile import TemporaryDirectory
from pathlib import Path

from settings_store import (
    PROVISIONAL_PULSES_PER_MM,
    SettingsValidationError,
    load_settings,
    save_settings,
    validate_pulses_per_mm,
)


class SettingsStoreTests(unittest.TestCase):
    def test_missing_file_returns_provisional_defaults(self):
        with TemporaryDirectory() as temp_dir:
            values, notice, provisional = load_settings(Path(temp_dir) / "settings.json")

        self.assertEqual(values, PROVISIONAL_PULSES_PER_MM)
        self.assertTrue(provisional)
        self.assertIn("PROVVISORI", notice)

    def test_save_and_reload(self):
        with TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "config" / "settings.json"
            expected = {"X": 80.0, "Y": 81.5, "Z": 400.0}

            self.assertEqual(save_settings(expected, path), expected)
            values, _, provisional = load_settings(path)
            payload = json.loads(path.read_text(encoding="utf-8"))

        self.assertEqual(values, expected)
        self.assertFalse(provisional)
        self.assertEqual(payload["schema_version"], 1)

    def test_invalid_values_are_rejected(self):
        for value in ("", "abc", "0", "-1", "nan", "inf"):
            with self.subTest(value=value):
                with self.assertRaises(SettingsValidationError):
                    validate_pulses_per_mm({"X": value, "Y": 80, "Z": 80})

    def test_corrupt_file_falls_back_without_overwriting_it(self):
        with TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "settings.json"
            path.write_text("{broken", encoding="utf-8")

            values, notice, provisional = load_settings(path)

            self.assertEqual(values, PROVISIONAL_PULSES_PER_MM)
            self.assertTrue(provisional)
            self.assertIn("non validi", notice)
            self.assertEqual(path.read_text(encoding="utf-8"), "{broken")


if __name__ == "__main__":
    unittest.main()
