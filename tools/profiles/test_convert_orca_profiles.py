#!/usr/bin/env python3
"""Checks of the option tables read from PrintConfig.cpp by convert_orca_profiles.py."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_orca_profiles  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


class OptionTypes(unittest.TestCase):
    def test_per_axis_machine_limits_are_known(self):
        types = convert_orca_profiles.tisma_option_types(ROOT)['types']
        for kind in ('feedrate', 'acceleration', 'jerk'):
            for axis in 'xyze':
                self.assertEqual(types.get('machine_max_%s_%s' % (kind, axis)), 'coFloats', (kind, axis))


if __name__ == '__main__':
    unittest.main()
