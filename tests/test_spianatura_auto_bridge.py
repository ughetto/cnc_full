import unittest

from dro_gui_tkinter_v16 import build_spianatura_auto_segments
from spianatura_toolpath import generate_spianatura_xy


class SpianaturaAutoBridgeTests(unittest.TestCase):
    def test_first_segment_positions_from_current_dro_to_start_xy_with_current_z(self):
        toolpath = generate_spianatura_xy(
            start_x=10.0,
            start_y=20.0,
            end_x=12.0,
            end_y=22.0,
            total_depth=1.0,
            depth_per_pass=1.0,
            tool_diameter=4.0,
            overlap=2.0,
            feed_xy=5.0,
            plunge_feed_z=0.5,
        )

        segments = build_spianatura_auto_segments(
            toolpath,
            current_position_mm={"X": 1.0, "Y": 2.0, "Z": 3.0},
            pulses_per_mm={"X": 10.0, "Y": 20.0, "Z": 30.0},
        )

        first = segments[0]
        self.assertEqual(first.target_x, 100)
        self.assertEqual(first.target_y, 400)
        self.assertEqual(first.target_z, 90)
        self.assertEqual(first.feed_mm_s, 5.0)

        second = segments[1]
        self.assertEqual(second.target_x, 100)
        self.assertEqual(second.target_y, 400)
        self.assertEqual(second.target_z, 30)


if __name__ == "__main__":
    unittest.main()
