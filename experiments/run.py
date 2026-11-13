"""Run paired latency interventions using a portable JSON experiment plan.

No analysis packages are required for simulation. Existing outputs are retained
unless --overwrite is passed. A quick run is a smoke check, not research data.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def load_plan(path, quick=False):
    plan = json.loads(path.read_text())
    if quick:
        plan.update(small_delays=[1, 2], epsilons=[0.01, 0.001],
                    seeds=plan['seeds'][:2], end_time=20000, burn_time=1000,
                    lookahead_time=1000, both_directions=False, reverse_at=[2])
    if plan['nodes'] < 2 or not 0 < plan['alpha'] < 1:
        raise ValueError('nodes >= 2 and 0 < alpha < 1 are required')
    if len(plan['seeds']) < 2 or len(set(plan['seeds'])) != len(plan['seeds']):
        raise ValueError('at least two distinct seeds are required')
    if len(set(plan['small_delays'])) != len(plan['small_delays']) or len(set(plan['epsilons'])) != len(plan['epsilons']):
        raise ValueError('delay and epsilon lists must not contain duplicates')
    for key in ['baseline_send', 'baseline_receive', 'burn_time', 'lookahead_time', 'end_time']:
        if not math.isfinite(plan[key]) or plan[key] < 0:
            raise ValueError('time and delay parameters must be finite and nonnegative')
    if plan['end_time'] <= plan['burn_time'] or plan['lookahead_time'] <= 0:
        raise ValueError('end_time > burn_time and lookahead_time > 0 are required')
    if not all(math.isfinite(x) and x >= 0 for x in plan['small_delays']):
        raise ValueError('small delays must be finite and nonnegative')
    if not all(math.isfinite(x) and 0 < x < 1 for x in plan['epsilons']):
        raise ValueError('epsilons must be in (0, 1)')
    return plan


def cases(plan):
    for small in plan['small_delays']:
        for seed in plan['seeds']:
            yield small, 'baseline', 0, 'baseline', seed
            directions = ['send', 'receive'] + (['both'] if plan['both_directions'] else [])
            for direction in directions:
                for epsilon in plan['epsilons']:
                    yield small, direction, epsilon, 'improve', seed
                    if small in plan['reverse_at']:
                        yield small, direction, epsilon, 'worsen', seed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT/'experiments/plans/marginal.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--overwrite', action='store_true')
    args = parser.parse_args()
    if args.workers < 1:
        parser.error('--workers must be positive')
    plan = load_plan(args.plan, args.quick)
    args.output.mkdir(parents=True, exist_ok=True)
    if any((args.output/name).exists() for name in ['runs.jsonl', 'plan.json']) and not args.overwrite:
        parser.error('output already exists; choose a new directory or pass --overwrite')
    subprocess.run(['make', 'build/latency_simulator'], cwd=ROOT, check=True)
    (args.output/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    cc = list(cases(plan))

    def run(case):
        small, direction, epsilon, kind, seed = case
        send, receive = plan['baseline_send'], plan['baseline_receive']
        multiplier = 1+epsilon if kind == 'worsen' else 1-epsilon
        if direction in ['send', 'both']:
            send *= multiplier
        if direction in ['receive', 'both']:
            receive *= multiplier
        command = [str(ROOT/'build/latency_simulator'), str(plan['nodes']), str(plan['alpha']),
                   str(send), str(receive), str(small), str(plan['end_time']),
                   str(plan['burn_time']), str(plan['lookahead_time']), str(seed)]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        row = json.loads(result.stdout)
        row.update(direction=direction, epsilon=epsilon, kind=kind)
        if row['accepted_A'] > row['generated_A']:
            raise RuntimeError('accepted reward exceeds generated blocks')
        if row['confirmation_blocks_reorganized']:
            raise RuntimeError('measured prefix changed during second continuation; increase lookahead')
        return row

    started = time.monotonic()
    with (args.output/'runs.jsonl').open('w') as out:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            for index, row in enumerate(pool.map(run, cc), 1):
                out.write(json.dumps(row)+'\n')
                if index % 32 == 0:
                    out.flush()
                    print(f'{index}/{len(cc)} runs', flush=True)
    manifest = {'runs': len(cc), 'elapsed_seconds': time.monotonic()-started,
                'quick': args.quick, 'python': sys.version,
                'source_sha256': hashlib.sha256((ROOT/'src/latency_simulator.cpp').read_bytes()).hexdigest(),
                'binary_sha256': hashlib.sha256((ROOT/'build/latency_simulator').read_bytes()).hexdigest(),
                'model': 'Constant-rate Poisson mining, directed fixed delays, first-seen, direct broadcasts.',
                'prefix_check': 'Two finite continuations; empirical stability, not a finality theorem.'}
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(f'Completed {len(cc)} runs: {args.output}', flush=True)


if __name__ == '__main__':
    main()
