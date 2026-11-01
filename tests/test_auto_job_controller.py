import unittest

from auto_job_controller import AutoJobController, AutoJobState, AutoSegment
from auto_protocol import decode_auto_message, encode_auto_message


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


class FakeTransport:
    def __init__(self):
        self.sent = []

    def send(self, line):
        self.sent.append(line)

    def messages(self, command=None):
        messages = [decode_auto_message(line) for line in self.sent]
        if command is not None:
            messages = [message for message in messages if message.command == command]
        return messages


class FakeTeensy:
    def __init__(self):
        self.job_id = None
        self.expected_segment_id = 1

    def receive(self, line):
        message = decode_auto_message(line)
        job_id = message.fields["JOB"]
        if message.command == "BEGIN":
            self.job_id = job_id
            self.expected_segment_id = 1
            return encode_auto_message("ACK", JOB=job_id, CMD="BEGIN")
        if message.command == "MOVE":
            segment_id = int(message.fields["ID"])
            if segment_id != self.expected_segment_id:
                return encode_auto_message("ERROR", JOB=job_id, CODE="BAD_ID", ID=segment_id)
            self.expected_segment_id += 1
            return encode_auto_message("ACK", JOB=job_id, CMD="MOVE", ID=segment_id)
        if message.command == "RUN":
            return encode_auto_message("ACK", JOB=job_id, CMD="RUN")
        if message.command == "STOP":
            return encode_auto_message("STOPPED", JOB=job_id, REASON="TEST")
        if message.command == "RESET":
            self.job_id = None
            return encode_auto_message("ACK", JOB=job_id, CMD="RESET")
        if message.command == "STATUS":
            return encode_auto_message("STATUS", JOB=job_id, STATE="IDLE", EXEC=0)
        raise AssertionError(f"Comando fake non gestito: {message.command}")


def make_segments(count):
    return [
        AutoSegment(index * 10, index * 20, index, 5.0, 100_000)
        for index in range(1, count + 1)
    ]


def start_test_job(controller, count):
    return controller.start_job(
        make_segments(count),
        current_position_steps=(0, 0, 0),
        pulses_per_mm=(80.0, 80.0, 400.0),
    )


class AutoJobControllerTests(unittest.TestCase):
    def make_controller(self, *, prefill=2, retries=2):
        self.clock = FakeClock()
        self.transport = FakeTransport()
        return AutoJobController(
            self.transport.send,
            clock=self.clock,
            job_id_factory=lambda: "TEST1",
            ack_timeout_s=0.5,
            max_retries=retries,
            prefill_segments=prefill,
        )

    def ack(self, controller, command, **fields):
        controller.handle_line(encode_auto_message("ACK", JOB="TEST1", CMD=command, **fields))

    def test_fake_teensy_happy_path_and_duplicate_ack(self):
        controller = self.make_controller(prefill=2)
        fake = FakeTeensy()
        start_test_job(controller, 2)

        begin = decode_auto_message(self.transport.sent[0])
        self.assertEqual(
            {key: begin.fields[key] for key in ("CX", "CY", "CZ", "PX", "PY", "PZ")},
            {"CX": "0", "CY": "0", "CZ": "0", "PX": "80", "PY": "80", "PZ": "400"},
        )

        controller.handle_line(fake.receive(self.transport.sent[0]))
        move_lines = self.transport.sent[1:3]
        first_ack = fake.receive(move_lines[0])
        controller.handle_line(first_ack)
        controller.handle_line(first_ack)
        controller.handle_line(fake.receive(move_lines[1]))
        controller.handle_line(fake.receive(self.transport.sent[-1]))

        self.assertEqual(controller.last_acked_segment, 2)
        self.assertEqual(controller.state, AutoJobState.RUNNING)

    def test_out_of_order_ack_stops_with_protocol_error(self):
        controller = self.make_controller(prefill=3)
        start_test_job(controller, 3)
        self.ack(controller, "BEGIN")

        self.ack(controller, "MOVE", ID=2)

        self.assertEqual(controller.state, AutoJobState.ERROR)
        self.assertIn("fuori ordine", controller.last_error)
        self.assertEqual(self.transport.messages()[-1].command, "STOP")

    def test_fake_teensy_rejects_out_of_order_segment(self):
        fake = FakeTeensy()
        fake.receive(encode_auto_message("BEGIN", JOB="TEST1", N=2))

        response = fake.receive(
            encode_auto_message(
                "MOVE", JOB="TEST1", ID=2, X=0, Y=0, Z=0, F=1.0, T=1000, END=0
            )
        )

        self.assertEqual(decode_auto_message(response).command, "ERROR")
        self.assertEqual(decode_auto_message(response).fields["CODE"], "BAD_ID")

    def test_timeout_retries_exact_line_then_sends_stop(self):
        controller = self.make_controller(retries=2)
        start_test_job(controller, 1)
        begin_line = self.transport.sent[0]

        for _ in range(2):
            self.clock.advance(0.5)
            controller.tick()
            self.assertEqual(self.transport.sent[-1], begin_line)
        self.clock.advance(0.5)
        controller.tick()

        self.assertEqual(controller.state, AutoJobState.ERROR)
        self.assertIn("Timeout", controller.last_error)
        self.assertEqual(self.transport.messages()[-1].command, "STOP")

    def test_buffer_low_refills_in_order(self):
        controller = self.make_controller(prefill=2)
        start_test_job(controller, 5)
        self.ack(controller, "BEGIN")
        self.ack(controller, "MOVE", ID=1)
        self.ack(controller, "MOVE", ID=2)
        self.ack(controller, "RUN")

        controller.handle_line(encode_auto_message("BUFFER_LOW", JOB="TEST1", Q=0, FREE=2))
        self.ack(controller, "MOVE", ID=3)
        self.ack(controller, "MOVE", ID=4)
        controller.handle_line(encode_auto_message("BUFFER_LOW", JOB="TEST1", Q=0, FREE=2))

        move_ids = [int(message.fields["ID"]) for message in self.transport.messages("MOVE")]
        self.assertEqual(move_ids, [1, 2, 3, 4, 5])
        self.assertEqual(controller.buffer_low_events, 2)

    def test_stop_is_clean_and_idempotent(self):
        controller = self.make_controller()
        start_test_job(controller, 2)

        self.assertTrue(controller.stop())
        self.assertTrue(controller.stop())
        self.assertEqual(len(self.transport.messages("STOP")), 1)
        controller.handle_line(encode_auto_message("STOPPED", JOB="TEST1", REASON="USER"))
        self.assertEqual(controller.state, AutoJobState.STOPPED)

    def test_non_auto_line_is_ignored(self):
        controller = self.make_controller()
        start_test_job(controller, 1)

        self.assertFalse(controller.handle_line("X:1,Y:2,Z:3"))
        self.assertEqual(controller.state, AutoJobState.BEGIN_SENT)

    def test_segment_data_is_validated_before_a_job(self):
        invalid_values = (
            {"target_x": 1.5},
            {"duration_us": 0},
            {"feed_mm_s": float("nan")},
        )
        base = {
            "target_x": 1,
            "target_y": 2,
            "target_z": 3,
            "feed_mm_s": 1.0,
            "duration_us": 1000,
        }
        for override in invalid_values:
            with self.subTest(override=override):
                values = dict(base)
                values.update(override)
                with self.assertRaises(ValueError):
                    AutoSegment(**values)

    def test_job_origin_and_axis_scales_are_required(self):
        controller = self.make_controller()
        segments = make_segments(1)

        with self.assertRaises(ValueError):
            controller.start_job(
                segments,
                current_position_steps=(0.5, 0, 0),
                pulses_per_mm=(80.0, 80.0, 400.0),
            )
        with self.assertRaises(ValueError):
            controller.start_job(
                segments,
                current_position_steps=(0, 0, 0),
                pulses_per_mm=(80.0, 0.0, 400.0),
            )

    def test_status_is_recorded_without_changing_state(self):
        controller = self.make_controller()
        start_test_job(controller, 1)

        controller.handle_line(
            encode_auto_message("STATUS", JOB="TEST1", STATE="BUFFERING", EXEC=0)
        )

        self.assertEqual(controller.state, AutoJobState.BEGIN_SENT)
        self.assertEqual(controller.last_status["STATE"], "BUFFERING")

    def test_reset_round_trip_returns_to_idle(self):
        controller = self.make_controller()
        start_test_job(controller, 1)
        controller.state = AutoJobState.STOPPED

        controller.request_reset()
        self.assertEqual(controller.state, AutoJobState.RESET_SENT)
        controller.handle_line(encode_auto_message("ACK", JOB="TEST1", CMD="RESET"))

        self.assertEqual(controller.state, AutoJobState.IDLE)
        self.assertIsNone(controller.job_id)


if __name__ == "__main__":
    unittest.main()
