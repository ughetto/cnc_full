import unittest

from spianatura_toolpath import ToolpathValidationError, generate_spianatura_xy


def make_toolpath(**overrides):
    values = {
        "start_x": 0.0,
        "start_y": 0.0,
        "end_x": 10.0,
        "end_y": 9.0,
        "total_depth": 1.0,
        "depth_per_pass": 0.4,
        "tool_diameter": 4.0,
        "overlap": 1.0,
        "feed_xy": 5.0,
        "plunge_feed_z": 0.5,
    }
    values.update(overrides)
    return generate_spianatura_xy(**values)


class SpianaturaToolpathTests(unittest.TestCase):
    def test_levels_passes_and_segment_count(self):
        result = make_toolpath()

        self.assertEqual(result.levels_z, (0.4, 0.8, 1.0))
        self.assertEqual(result.passes_per_level, 4)
        self.assertEqual(result.total_passes, 12)
        self.assertEqual(result.segment_count, 25)
        self.assertEqual(result.final_cut_point.z, 1.0)
        self.assertEqual(result.final_point.z, -29.0)
        self.assertEqual(result.segments[-1].kind, "RETRACT")

    def test_serpentine_reverses_y_order_at_each_level(self):
        result = make_toolpath(total_depth=0.8)
        cuts_level_1 = [s.end for s in result.segments if s.kind == "CUT" and s.level == 1]
        cuts_level_2 = [s.end for s in result.segments if s.kind == "CUT" and s.level == 2]

        self.assertEqual([point.y for point in cuts_level_1], [0.0, 3.0, 6.0, 9.0])
        self.assertEqual([point.x for point in cuts_level_1], [10.0, 0.0, 10.0, 0.0])
        self.assertEqual([point.y for point in cuts_level_2], [9.0, 6.0, 3.0, 0.0])
        self.assertEqual([point.x for point in cuts_level_2], [10.0, 0.0, 10.0, 0.0])

    def test_end_boundary_is_always_included_and_limits_are_not_offset(self):
        result = make_toolpath(end_y=10.0)
        first_level_y = [
            segment.end.y
            for segment in result.segments
            if segment.kind == "CUT" and segment.level == 1
        ]

        self.assertEqual(first_level_y, [0.0, 3.0, 6.0, 9.0, 10.0])
        self.assertEqual(result.initial_point.x, 0.0)
        self.assertEqual(result.initial_point.y, 0.0)

    def test_reversed_area_coordinates_are_normalized(self):
        result = make_toolpath(start_x=10.0, start_y=9.0, end_x=-2.0, end_y=-3.0)

        self.assertEqual(result.initial_point.x, -2.0)
        self.assertEqual(result.initial_point.y, -3.0)

    def test_all_segments_are_continuous(self):
        result = make_toolpath()

        for previous, following in zip(result.segments, result.segments[1:]):
            self.assertEqual(previous.end, following.start)

    def test_invalid_parameters_are_rejected(self):
        invalid_cases = (
            {"end_x": 0.0},
            {"end_y": 0.0},
            {"total_depth": 0.0},
            {"depth_per_pass": -1.0},
            {"tool_diameter": 4.0, "overlap": 4.0},
            {"overlap": -0.1},
            {"feed_xy": 0.0},
            {"plunge_feed_z": 0.0},
        )
        for values in invalid_cases:
            with self.subTest(values=values):
                with self.assertRaises(ToolpathValidationError):
                    make_toolpath(**values)


if __name__ == "__main__":
    unittest.main()
