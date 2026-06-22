import unittest

from auto_protocol import (
    AutoCrcError,
    AutoProtocolError,
    crc16_ccitt,
    decode_auto_message,
    encode_auto_message,
)


class AutoProtocolTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_encode_decode_round_trip(self):
        line = encode_auto_message(
            "MOVE", JOB="TEST1", ID=7, X=-120, Y=80, Z=40, F=5.5, T=200000, END=False
        )
        message = decode_auto_message(line)

        self.assertTrue(line.startswith("AUTO,MOVE,"))
        self.assertTrue(line.endswith("\n"))
        self.assertEqual(message.command, "MOVE")
        self.assertEqual(message.fields["JOB"], "TEST1")
        self.assertEqual(message.fields["ID"], "7")
        self.assertEqual(message.fields["X"], "-120")
        self.assertEqual(message.fields["END"], "0")

    def test_tampered_payload_fails_crc(self):
        line = encode_auto_message("RUN", JOB="TEST1").replace("RUN", "STOP", 1)
        with self.assertRaises(AutoCrcError):
            decode_auto_message(line)

    def test_invalid_or_non_auto_lines_are_rejected(self):
        for line in ("X:1,Y:2,Z:3", "JOG,X:1", "AUTO,RUN,JOB:X"):
            with self.subTest(line=line):
                with self.assertRaises(AutoProtocolError):
                    decode_auto_message(line)


if __name__ == "__main__":
    unittest.main()
