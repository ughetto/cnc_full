import json
import math
import os
from pathlib import Path
import tempfile


SCHEMA_VERSION = 1
DEFAULT_CONFIG_PATH = Path.home() / ".config" / "cnc_controller" / "settings.json"

# Valori utilizzabili dalla sola applicazione, ma non ancora verificati sulla macchina.
PROVISIONAL_PULSES_PER_MM = {"X": 80.0, "Y": 80.0, "Z": 80.0}
MAX_PULSES_PER_MM = 1_000_000.0


class SettingsError(Exception):
    pass


class SettingsValidationError(SettingsError):
    pass


def validate_pulses_per_mm(values):
    validated = {}
    for axis in ("X", "Y", "Z"):
        if axis not in values:
            raise SettingsValidationError(f"Valore impulsi/mm {axis} mancante")

        raw_value = values[axis]
        if isinstance(raw_value, bool):
            raise SettingsValidationError(f"Il valore {axis} non è numerico")

        try:
            value = float(str(raw_value).strip().replace(",", "."))
        except (TypeError, ValueError):
            raise SettingsValidationError(f"Il valore {axis} non è numerico") from None

        if not math.isfinite(value) or value <= 0.0:
            raise SettingsValidationError(f"Il valore {axis} deve essere maggiore di zero")
        if value > MAX_PULSES_PER_MM:
            raise SettingsValidationError(
                f"Il valore {axis} supera il limite di {MAX_PULSES_PER_MM:g} impulsi/mm"
            )
        validated[axis] = value

    return validated


def load_settings(path=DEFAULT_CONFIG_PATH):
    path = Path(path)
    if not path.exists():
        return (
            dict(PROVISIONAL_PULSES_PER_MM),
            "File settings non presente: sono attivi DEFAULT PROVVISORI da verificare.",
            True,
        )

    try:
        with path.open("r", encoding="utf-8") as config_file:
            payload = json.load(config_file)
        if payload.get("schema_version") != SCHEMA_VERSION:
            raise SettingsValidationError("versione del file settings non supportata")
        values = validate_pulses_per_mm(payload.get("pulses_per_mm", {}))
    except (OSError, json.JSONDecodeError, AttributeError, SettingsValidationError) as exc:
        return (
            dict(PROVISIONAL_PULSES_PER_MM),
            f"Settings non validi ({exc}): sono attivi DEFAULT PROVVISORI.",
            True,
        )

    return values, f"Settings caricati da {path}", False


def save_settings(values, path=DEFAULT_CONFIG_PATH):
    validated = validate_pulses_per_mm(values)
    path = Path(path)
    payload = {
        "schema_version": SCHEMA_VERSION,
        "pulses_per_mm": validated,
    }

    temp_path = None
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temp_file:
            temp_path = Path(temp_file.name)
            json.dump(payload, temp_file, indent=2, sort_keys=True)
            temp_file.write("\n")
            temp_file.flush()
            os.fsync(temp_file.fileno())

        os.replace(temp_path, path)
        temp_path = None

        try:
            directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except (AttributeError, OSError):
            pass
    except OSError as exc:
        raise SettingsError(f"Impossibile salvare {path}: {exc}") from exc
    finally:
        if temp_path is not None:
            try:
                temp_path.unlink()
            except OSError:
                pass

    return validated
