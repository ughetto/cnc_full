from dataclasses import dataclass
import re


AUTO_PREFIX = "AUTO"
_TOKEN_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")


class AutoProtocolError(ValueError):
    pass


class AutoCrcError(AutoProtocolError):
    pass


@dataclass(frozen=True)
class AutoMessage:
    command: str
    fields: dict


def crc16_ccitt(data: bytes, initial=0xFFFF) -> int:
    crc = initial
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _encode_value(value):
    if isinstance(value, bool):
        value = "1" if value else "0"
    elif isinstance(value, float):
        value = format(value, ".12g")
    else:
        value = str(value)

    if (not value or not value.isascii() or
            any(character in value for character in ",:*\r\n")):
        raise AutoProtocolError(f"Valore non valido nel protocollo AUTO: {value!r}")
    return value


def encode_auto_message(command, **fields):
    command = str(command).upper()
    if not _TOKEN_RE.fullmatch(command):
        raise AutoProtocolError(f"Comando AUTO non valido: {command!r}")

    tokens = [AUTO_PREFIX, command]
    for key, value in fields.items():
        key = str(key).upper()
        if not _TOKEN_RE.fullmatch(key):
            raise AutoProtocolError(f"Campo AUTO non valido: {key!r}")
        tokens.append(f"{key}:{_encode_value(value)}")

    payload = ",".join(tokens)
    checksum = crc16_ccitt(payload.encode("ascii"))
    return f"{payload}*{checksum:04X}\n"


def decode_auto_message(line):
    if isinstance(line, bytes):
        try:
            line = line.decode("ascii")
        except UnicodeDecodeError:
            raise AutoProtocolError("Messaggio AUTO non ASCII") from None

    line = str(line).strip()
    if not line.startswith(f"{AUTO_PREFIX},"):
        raise AutoProtocolError("La riga non è un messaggio AUTO")
    if "*" not in line:
        raise AutoProtocolError("CRC AUTO mancante")

    payload, checksum_text = line.rsplit("*", 1)
    if not re.fullmatch(r"[0-9A-Fa-f]{4}", checksum_text):
        raise AutoProtocolError("Formato CRC AUTO non valido")

    expected = crc16_ccitt(payload.encode("ascii"))
    received = int(checksum_text, 16)
    if received != expected:
        raise AutoCrcError(f"CRC AUTO errato: ricevuto {received:04X}, atteso {expected:04X}")

    tokens = payload.split(",")
    if len(tokens) < 2 or tokens[0] != AUTO_PREFIX or not _TOKEN_RE.fullmatch(tokens[1]):
        raise AutoProtocolError("Intestazione AUTO non valida")

    fields = {}
    for token in tokens[2:]:
        if ":" not in token:
            raise AutoProtocolError(f"Campo AUTO senza separatore: {token!r}")
        key, value = token.split(":", 1)
        if not _TOKEN_RE.fullmatch(key) or not value:
            raise AutoProtocolError(f"Campo AUTO non valido: {token!r}")
        if key in fields:
            raise AutoProtocolError(f"Campo AUTO duplicato: {key}")
        fields[key] = value

    return AutoMessage(tokens[1], fields)


def require_int(message, field):
    try:
        return int(message.fields[field])
    except KeyError:
        raise AutoProtocolError(f"Campo AUTO mancante: {field}") from None
    except ValueError:
        raise AutoProtocolError(f"Campo AUTO non intero: {field}") from None


def require_field(message, field):
    try:
        return message.fields[field]
    except KeyError:
        raise AutoProtocolError(f"Campo AUTO mancante: {field}") from None
