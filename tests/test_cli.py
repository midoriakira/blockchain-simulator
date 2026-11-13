import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LATENCY = ROOT/'build/latency_simulator'
UNIFORM = ROOT/'build/uniform_simulator'


def latency(*args):
    result = subprocess.run([str(LATENCY), *map(str, args)], check=True,
                            capture_output=True, text=True)
    return json.loads(result.stdout)


class SimulatorChecks(unittest.TestCase):
    def test_original_source_is_unchanged(self):
        data = (ROOT/'blockchainSimulator.cpp').read_bytes()
        digest = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        self.assertEqual(digest, 'ebadfd662a3cddade3b4aaa0bfe9b7008753c96b')

    def test_zero_delay_adopts_every_generated_A(self):
        row = latency(1000, .3, 0, 0, 0, 10000, 1000, 1000, 10)
        self.assertEqual(row['accepted_A'], row['generated_A'])
        self.assertEqual(row['acceptance_A'], 1)
        self.assertEqual(row['confirmation_blocks_reorganized'], 0)
        self.assertAlmostEqual(row['reward_rate_A'], row['reward_share_A']*row['chain_rate'])

    def test_empty_window_ratios_are_null(self):
        row = latency(10, .3, 1, 1, 1, 1e-15, 0, 1e-15, 1)
        self.assertEqual(row['accepted_all'], 0)
        self.assertIsNone(row['reward_share_A'])
        self.assertIsNone(row['acceptance_A'])
        self.assertEqual(row['reward_rate_A'], 0)

    def test_repeated_seed_and_paired_generation(self):
        args = (1000, .3, 1, 1, 2, 10000, 1000, 1000, 2001)
        baseline = latency(*args)
        self.assertEqual(baseline, latency(*args))
        improved = latency(1000, .3, .9999, 1, 2, 10000, 1000, 1000, 2001)
        self.assertEqual(baseline['generated_A'], improved['generated_A'])

    def test_nonfinite_and_invalid_inputs_are_rejected(self):
        cases = [(.3, 'nan', 1, 1, 100, 0, 10),
                 (.3, 1, 1, 1, 'nan', 0, 10),
                 (.3, 1, -1, 1, 100, 0, 10),
                 (1, 1, 1, 1, 100, 0, 10)]
        for values in cases:
            with self.subTest(values=values):
                result = subprocess.run([str(LATENCY), '10', *map(str, values), '1'],
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')

    def test_uniform_zero_delay_reward_accounting(self):
        result = subprocess.run([str(UNIFORM), 'simulate', '10', '3', '10', '0',
                                 '2000', '10', '100'], check=True, capture_output=True, text=True)
        row = json.loads(result.stdout)
        self.assertEqual(row['A_wins_on_O_start'], 0)
        self.assertEqual(row['A_wins_on_A_start'], row['A_start_count'])
        self.assertEqual(row['r_A'], row['pi_A'])
        self.assertEqual(row['prefix_blocks_changed_after_lookahead'], 0)

    def test_experiment_plan_sizes(self):
        spec = importlib.util.spec_from_file_location('experiment_runner', ROOT/'experiments/run.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for name, count in [('pilot', 352), ('marginal', 1136)]:
            plan = module.load_plan(ROOT/f'experiments/plans/{name}.json')
            self.assertEqual(len(list(module.cases(plan))), count)

    def test_quick_pipeline_keeps_pairs(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([sys.executable, str(ROOT/'experiments/run.py'), '--quick',
                            '--output', directory, '--workers', '2'], check=True,
                           capture_output=True, text=True)
            rows = [json.loads(line) for line in (Path(directory)/'runs.jsonl').read_text().splitlines()]
            self.assertEqual(len(rows), 28)
            base = {(r['small_delay'], r['seed']): r for r in rows if r['kind'] == 'baseline'}
            for row in rows:
                self.assertEqual(row['generated_A'], base[row['small_delay'], row['seed']]['generated_A'])
                self.assertEqual(row['confirmation_blocks_reorganized'], 0)


if __name__ == '__main__':
    unittest.main()
