"""Checks for lab/realism/score.py: python lab/realism/test_score.py"""
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import score  # noqa: E402


def scene(seed=1, sky=(0.45, 0.62, 0.86), height=288):
    """A street-like test card: sky on top, a textured ground, a few vertical 'buildings'."""
    rng = np.random.default_rng(seed)
    image = np.zeros((height, score.WIDTH, 3))
    horizon = height // 2
    image[:horizon] = sky
    ground = 0.35 + 0.08 * rng.standard_normal((height - horizon, score.WIDTH, 1))
    image[horizon:] = np.clip(ground * np.array([0.9, 0.85, 0.8]), 0, 1)
    for x in rng.integers(0, score.WIDTH - 60, 6):
        image[horizon - 90:horizon, x:x + 50] = rng.uniform(0.3, 0.95, 3)
    return image


class ScoreTest(unittest.TestCase):
    def test_identical_is_100(self):
        a = scene()
        total, parts, _ = score.score_pair(a, a)
        self.assertEqual(total, 100.0)
        self.assertTrue(all(v == 100.0 for v in parts.values()))

    def test_small_changes_score_high_and_big_changes_low(self):
        a = scene()
        darker = score.score_pair(np.clip(a * 0.9, 0, 1), a)[0]
        other = score.score_pair(scene(seed=7, sky=(0.75, 0.75, 0.78)), a)[0]
        night = score.score_pair(a * 0.08, a)[0]
        self.assertGreater(darker, 75)
        self.assertLess(other, darker)
        self.assertLess(night, other)

    def test_sky_part_sees_the_sky(self):
        a = scene()
        grey = scene(sky=(0.7, 0.7, 0.7))
        self.assertLess(score.score_pair(grey, a)[1]["sky"], 60)
        self.assertGreater(score.score_pair(grey, a)[1]["structure"], 80)

    def test_smooth_game_loses_detail(self):
        a = scene()
        smooth = a.copy()
        smooth[a.shape[0] // 2:] = a[a.shape[0] // 2:].mean(axis=(0, 1))
        self.assertLess(score.score_pair(smooth, a)[1]["detail"], 75)

    def test_weights_sum(self):
        self.assertEqual(set(score.WEIGHTS), set(score.SCALE))


if __name__ == "__main__":
    unittest.main()
