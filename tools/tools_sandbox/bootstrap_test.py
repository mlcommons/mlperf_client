import unittest

from bootstrap_mean_ci import bootstrap_mean_ci

N_ITERS=100000
N_TESTS=20


class TestBootstrapMeanCI(unittest.TestCase):
    def test_empty_raises(self):
        with self.assertRaises(ValueError):
            bootstrap_mean_ci([])

    def test_invalid_ci_level(self):
        with self.assertRaises(ValueError):
            bootstrap_mean_ci([1.0], ci_level=1.0)

    def test_invalid_iterations(self):
        with self.assertRaises(ValueError):
            bootstrap_mean_ci([1.0], n_iterations=0)

    def test_constant_sample(self):
        mean, lo, hi = bootstrap_mean_ci(
            [5.0, 5.0, 5.0], n_iterations=N_ITERS, seed=0
        )
        self.assertEqual(mean, 5.0)
        self.assertAlmostEqual(lo, 5.0, places=9)
        self.assertAlmostEqual(hi, 5.0, places=9)

    def test_reproducible_with_seed(self):
        data = [1.0, 2.0, 3.0, 4.0]
        a = bootstrap_mean_ci(data, n_iterations=N_ITERS, seed=123)
        b = bootstrap_mean_ci(data, n_iterations=N_ITERS, seed=123)
        self.assertEqual(a, b)

    def test_sample_mean(self):
        for s in range(N_TESTS):
          mean, _, _ = bootstrap_mean_ci([2.0, 4.0, 6.0], n_iterations=N_ITERS, seed=s)
          self.assertAlmostEqual(mean, 4.0)

    def test_ci_bounds_ordered(self):
        for s in range(N_TESTS):
          _, lo, hi = bootstrap_mean_ci(
              [0.1, 0.5, 0.9, 1.2, 0.3], n_iterations=N_ITERS, seed=s
          )
          self.assertLess(lo, hi)

    def test_point_estimate_inside_ci_typical(self):
        for s in range(N_TESTS):
          mean, lo, hi = bootstrap_mean_ci(
              [1.0, 2.0, 3.0, 4.0, 5.0], n_iterations=N_ITERS, seed=s
          )
          self.assertLessEqual(lo, mean)
          self.assertGreaterEqual(hi, mean)


if __name__ == "__main__":
    unittest.main()
