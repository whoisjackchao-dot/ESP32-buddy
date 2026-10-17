"""M6 increment 2: device control-line parsing tests (contract M6 §8.1).

Covers BTN-05 host side: the device shares one TX stream between
ESP-IDF logs and control commands -- a line starting with '{' is a
JSON control command, everything else is a log line the host ignores.
Pure parsing only; no serial port is opened.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pet_devio


class ParseDeviceLine(unittest.TestCase):
    def test_valid_back_window_command(self):
        cmd = pet_devio.parse_device_line('{"v":1,"cmd":"back_window"}\n')
        self.assertEqual(cmd, {"v": 1, "cmd": "back_window"})

    def test_whitespace_is_stripped(self):
        cmd = pet_devio.parse_device_line('  {"v":1,"cmd":"back_window"} \r\n')
        self.assertEqual(cmd, {"v": 1, "cmd": "back_window"})

    def test_log_line_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line("I (123) tag: boot ok\n"))

    def test_blank_line_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line("   \n"))
        self.assertIsNone(pet_devio.parse_device_line(""))

    def test_invalid_json_starting_with_brace_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('{"v":1,"cmd":'))
        self.assertIsNone(pet_devio.parse_device_line("{not json"))

    def test_json_array_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('[1,2]\n'))

    def test_version_2_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('{"v":2,"cmd":"back_window"}'))

    def test_version_true_returns_none(self):
        # bool is not accepted for v even though True == 1
        self.assertIsNone(pet_devio.parse_device_line('{"v":true,"cmd":"back_window"}'))

    def test_non_string_cmd_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('{"v":1,"cmd":123}'))

    def test_empty_cmd_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('{"v":1,"cmd":""}'))

    def test_missing_cmd_returns_none(self):
        self.assertIsNone(pet_devio.parse_device_line('{"v":1}'))

    def test_extra_keys_allowed(self):
        cmd = pet_devio.parse_device_line('{"v":1,"cmd":"back_window","seq":7}')
        self.assertIsNotNone(cmd)
        self.assertEqual(cmd["seq"], 7)


if __name__ == "__main__":
    unittest.main()
