from dataclasses import dataclass
from enum import Enum
import math
import time
import uuid

from auto_protocol import (
    AutoProtocolError,
    decode_auto_message,
    encode_auto_message,
    require_field,
    require_int,
)


class AutoJobState(Enum):
    IDLE = "IDLE"
    BEGIN_SENT = "BEGIN_SENT"
    BUFFERING = "BUFFERING"
    RUN_SENT = "RUN_SENT"
    RUNNING = "RUNNING"
    STOPPING = "STOPPING"
    RESET_SENT = "RESET_SENT"
    STOPPED = "STOPPED"
    COMPLETED = "COMPLETED"
    ERROR = "ERROR"


@dataclass(frozen=True)
class AutoSegment:
    target_x: int
    target_y: int
    target_z: int
    feed_mm_s: float
    duration_us: int

    def __post_init__(self):
        for axis, value in (("X", self.target_x), ("Y", self.target_y), ("Z", self.target_z)):
            if isinstance(value, bool) or not isinstance(value, int):
                raise ValueError(f"Il target {axis} deve essere un numero intero di step")
        if isinstance(self.duration_us, bool) or not isinstance(self.duration_us, int):
            raise ValueError("La durata del segmento deve essere espressa in microsecondi interi")
        if self.duration_us <= 0:
            raise ValueError("La durata del segmento deve essere maggiore di zero")
        if (isinstance(self.feed_mm_s, bool) or
                not isinstance(self.feed_mm_s, (int, float)) or
                not math.isfinite(self.feed_mm_s) or self.feed_mm_s <= 0):
            raise ValueError("Il feed del segmento deve essere maggiore di zero")


@dataclass
class _PendingMessage:
    line: str
    sent_at: float
    retries: int = 0


class AutoJobController:
    def __init__(
        self,
        send_line,
        *,
        clock=time.monotonic,
        job_id_factory=None,
        ack_timeout_s=0.5,
        max_retries=3,
        prefill_segments=16,
    ):
        if ack_timeout_s <= 0:
            raise ValueError("ack_timeout_s deve essere maggiore di zero")
        if max_retries < 0:
            raise ValueError("max_retries non può essere negativo")
        if prefill_segments <= 0:
            raise ValueError("prefill_segments deve essere maggiore di zero")

        self.send_line = send_line
        self.clock = clock
        self.job_id_factory = job_id_factory or (lambda: uuid.uuid4().hex[:8].upper())
        self.ack_timeout_s = float(ack_timeout_s)
        self.max_retries = int(max_retries)
        self.prefill_segments = int(prefill_segments)
        self.reset()

    def reset(self):
        self.state = AutoJobState.IDLE
        self.job_id = None
        self.segments = ()
        self.pending = {}
        self.next_segment_to_send = 1
        self.last_acked_segment = 0
        self.buffer_low_events = 0
        self.last_error = None
        self.last_status = None

    def start_job(self, segments, *, current_position_steps, pulses_per_mm):
        if self.state not in (
            AutoJobState.IDLE,
            AutoJobState.COMPLETED,
            AutoJobState.STOPPED,
            AutoJobState.ERROR,
        ):
            raise RuntimeError(f"Job non avviabile nello stato {self.state.value}")

        segments = tuple(segments)
        if not segments:
            raise ValueError("Il job deve contenere almeno un segmento")
        if not all(isinstance(segment, AutoSegment) for segment in segments):
            raise TypeError("Tutti i segmenti devono essere AutoSegment")

        current_position_steps = tuple(current_position_steps)
        pulses_per_mm = tuple(pulses_per_mm)
        if len(current_position_steps) != 3 or any(
            isinstance(value, bool) or not isinstance(value, int)
            for value in current_position_steps
        ):
            raise ValueError("current_position_steps deve contenere tre interi X/Y/Z")
        if len(pulses_per_mm) != 3 or any(
            isinstance(value, bool) or
            not isinstance(value, (int, float)) or
            not math.isfinite(value) or value <= 0
            for value in pulses_per_mm
        ):
            raise ValueError("pulses_per_mm deve contenere tre valori positivi X/Y/Z")

        self.reset()
        self.job_id = str(self.job_id_factory())
        self.segments = segments
        self.current_position_steps = current_position_steps
        self.pulses_per_mm = tuple(float(value) for value in pulses_per_mm)
        self.state = AutoJobState.BEGIN_SENT
        self._send_pending(
            "BEGIN",
            encode_auto_message(
                "BEGIN",
                JOB=self.job_id,
                N=len(self.segments),
                CX=current_position_steps[0],
                CY=current_position_steps[1],
                CZ=current_position_steps[2],
                PX=self.pulses_per_mm[0],
                PY=self.pulses_per_mm[1],
                PZ=self.pulses_per_mm[2],
            ),
        )
        return self.job_id

    def handle_line(self, line):
        if isinstance(line, bytes):
            is_auto = line.startswith(b"AUTO,")
        else:
            is_auto = str(line).lstrip().startswith("AUTO,")
        if not is_auto:
            return False

        try:
            message = decode_auto_message(line)
            if message.command == "STATUS":
                self.last_status = dict(message.fields)
                return True
            message_job = require_field(message, "JOB")
            if self.job_id is None or message_job != self.job_id:
                raise AutoProtocolError(f"JOB inatteso: {message_job}")
            self._dispatch_message(message)
        except AutoProtocolError as exc:
            self._protocol_failure(str(exc))
        return True

    def tick(self):
        if self.state in (
            AutoJobState.IDLE,
            AutoJobState.COMPLETED,
            AutoJobState.STOPPED,
            AutoJobState.ERROR,
        ):
            return

        now = self.clock()
        for key, pending in list(self.pending.items()):
            if now - pending.sent_at < self.ack_timeout_s:
                continue
            if pending.retries >= self.max_retries:
                self._timeout_failure(key)
                return
            self.send_line(pending.line)
            pending.sent_at = now
            pending.retries += 1

    def stop(self, reason="USER"):
        if self.state in (AutoJobState.IDLE, AutoJobState.COMPLETED, AutoJobState.STOPPED):
            return False
        if self.state == AutoJobState.STOPPING:
            return True

        self.pending.clear()
        self.state = AutoJobState.STOPPING
        self._send_pending(
            "STOP",
            encode_auto_message("STOP", JOB=self.job_id, REASON=reason),
        )
        return True

    def request_reset(self):
        if self.state not in (
            AutoJobState.IDLE,
            AutoJobState.COMPLETED,
            AutoJobState.STOPPED,
            AutoJobState.ERROR,
        ):
            raise RuntimeError(f"RESET non consentito nello stato {self.state.value}")
        if self.job_id is None:
            self.job_id = str(self.job_id_factory())
        self.pending.clear()
        self.state = AutoJobState.RESET_SENT
        self._send_pending(
            "RESET",
            encode_auto_message("RESET", JOB=self.job_id),
        )

    def request_status(self):
        self.send_line(encode_auto_message("STATUS", JOB=self.job_id or "STATUS"))

    def _dispatch_message(self, message):
        if message.command == "ACK":
            self._handle_ack(message)
        elif message.command == "BUFFER_LOW":
            self._handle_buffer_low(message)
        elif message.command == "COMPLETED":
            self.pending.clear()
            self.state = AutoJobState.COMPLETED
        elif message.command == "STOPPED":
            self.pending.clear()
            self.state = AutoJobState.STOPPED
        elif message.command == "ERROR":
            self.pending.clear()
            self.last_error = message.fields.get("CODE", "TEENSY_ERROR")
            self.state = AutoJobState.ERROR
        else:
            raise AutoProtocolError(f"Risposta AUTO non gestita: {message.command}")

    def _handle_ack(self, message):
        command = require_field(message, "CMD")
        if command == "BEGIN":
            if "BEGIN" not in self.pending:
                return
            del self.pending["BEGIN"]
            self.state = AutoJobState.BUFFERING
            self._send_more_segments(self.prefill_segments)
            return

        if command == "MOVE":
            segment_id = require_int(message, "ID")
            if segment_id <= self.last_acked_segment:
                return
            expected = self.last_acked_segment + 1
            if segment_id != expected or f"MOVE:{segment_id}" not in self.pending:
                raise AutoProtocolError(
                    f"ACK MOVE fuori ordine: ricevuto {segment_id}, atteso {expected}"
                )
            del self.pending[f"MOVE:{segment_id}"]
            self.last_acked_segment = segment_id
            if self.state == AutoJobState.BUFFERING and not self._has_pending_moves():
                self.state = AutoJobState.RUN_SENT
                self._send_pending(
                    "RUN",
                    encode_auto_message("RUN", JOB=self.job_id),
                )
            return

        if command == "RUN":
            if "RUN" not in self.pending:
                return
            del self.pending["RUN"]
            self.state = AutoJobState.RUNNING
            return

        if command == "STOP":
            if self.state == AutoJobState.STOPPING:
                self.pending.pop("STOP", None)
            return

        if command == "RESET":
            if "RESET" not in self.pending:
                return
            self.reset()
            return

        raise AutoProtocolError(f"ACK per comando sconosciuto: {command}")

    def _handle_buffer_low(self, message):
        if self.state != AutoJobState.RUNNING:
            raise AutoProtocolError(f"BUFFER_LOW inatteso nello stato {self.state.value}")
        free_slots = require_int(message, "FREE")
        if free_slots < 0:
            raise AutoProtocolError("BUFFER_LOW con FREE negativo")
        self.buffer_low_events += 1
        self._send_more_segments(min(free_slots, self.prefill_segments))

    def _send_more_segments(self, limit):
        sent = 0
        while self.next_segment_to_send <= len(self.segments) and sent < limit:
            segment_id = self.next_segment_to_send
            segment = self.segments[segment_id - 1]
            line = encode_auto_message(
                "MOVE",
                JOB=self.job_id,
                ID=segment_id,
                X=segment.target_x,
                Y=segment.target_y,
                Z=segment.target_z,
                F=segment.feed_mm_s,
                T=segment.duration_us,
                END=segment_id == len(self.segments),
            )
            self._send_pending(f"MOVE:{segment_id}", line)
            self.next_segment_to_send += 1
            sent += 1

    def _send_pending(self, key, line):
        self.send_line(line)
        self.pending[key] = _PendingMessage(line=line, sent_at=self.clock())

    def _has_pending_moves(self):
        return any(key.startswith("MOVE:") for key in self.pending)

    def _protocol_failure(self, reason):
        self.last_error = reason
        self.pending.clear()
        self.state = AutoJobState.ERROR
        if self.job_id is not None:
            self.send_line(encode_auto_message("STOP", JOB=self.job_id, REASON="PROTOCOL_ERROR"))

    def _timeout_failure(self, key):
        self.last_error = f"Timeout ACK esaurito per {key}"
        self.pending.clear()
        self.state = AutoJobState.ERROR
        if self.job_id is not None:
            self.send_line(encode_auto_message("STOP", JOB=self.job_id, REASON="ACK_TIMEOUT"))
