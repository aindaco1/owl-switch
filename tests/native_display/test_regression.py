"""Fail-closed regression fixtures for asynchronous native observations."""
import copy
import unittest
from unittest.mock import patch
import regression


SCREEN = {"x": 1920, "y": 0, "width": 1920, "height": 1080}
FRAME = {"X": 1920, "Y": 0, "Width": 1920, "Height": 1080}


def snapshot():
    return {"windows": [{"frame": dict(FRAME), "layer": 0, "alpha": 1}],
            "accessibleWindows": [{**FRAME, "valid": True, "buttons": []}],
            "accessibilityErrors": [], "frontPID": 20}


class Clock:
    elapsed = 0

    def monotonic(self):
        return self.elapsed

    def sleep(self, duration):
        self.elapsed += duration


class Probe:
    def __init__(self, clock, observations):
        self.clock, self.observations = clock, list(observations)

    def window(self, *_):
        self.clock.elapsed += 0.5
        value = self.observations.pop(0) if len(self.observations) > 1 else self.observations[0]
        return copy.deepcopy(value)


class IPC:
    def get(self, key):
        return key == "fullscreen"


class WindowObservationTest(unittest.TestCase):
    def observe(self, *observations):
        clock = Clock()
        app = object.__new__(regression.AppCase)
        app.tools, app.mpv, app.ipc = Probe(clock, observations), 10, IPC()
        with patch.object(regression, "time", clock):
            result = app.snapshot()
        return result, clock.elapsed

    def test_waits_for_native_visibility_after_decoder_readiness(self):
        pending = snapshot()
        pending["windows"] = []
        pending["accessibleWindows"] = []
        result, _ = self.observe(pending, snapshot())
        self.assertEqual(regression.window_failures(result, SCREEN, 20), [])
        self.assertTrue(result["readinessAttempts"])

    def test_does_not_wait_away_wrong_bounds_chrome_or_focus(self):
        unavailable = snapshot()
        unavailable["accessibilityErrors"] = [{"attribute": "AXWindows", "error": -25204}]
        wrong = snapshot()
        wrong["windows"][0]["frame"]["Width"] = 640
        wrong["accessibleWindows"][0]["buttons"] = [{"kind": "AXCloseButton"}]
        wrong["frontPID"] = 30
        result, _ = self.observe(unavailable, wrong, snapshot())
        self.assertEqual(regression.window_failures(result, SCREEN, 20), [
            "video does not cover the selected display", "video exposes title-bar controls",
            "foreground app changed"])

    def test_persistent_inspection_errors_are_bounded_and_fail_closed(self):
        unavailable = snapshot()
        unavailable["accessibilityErrors"] = [{"attribute": "AXCloseButton", "error": -25204}]
        result, elapsed = self.observe(unavailable)
        self.assertGreaterEqual(elapsed, 8)
        self.assertLess(elapsed, 9)
        self.assertIn("Accessibility window inspection failed",
                      regression.window_failures(result, SCREEN, 20))

    def test_failed_geometry_is_not_an_accessible_borderless_window(self):
        unavailable = snapshot()
        unavailable["accessibleWindows"][0].update(valid=False, Width=0, Height=0)
        result, _ = self.observe(unavailable)
        self.assertIn("Accessibility window inspection failed",
                      regression.window_failures(result, SCREEN, 20))


if __name__ == "__main__":
    unittest.main()
