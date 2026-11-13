"""Paired d/T x largest-hash-share sweep, with cell-specific small steps.

All delays are direct fixed broadcasts, T=1. The distinguished miner changes
all its outbound or all its inbound delays, holding other delays fixed.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def load_plan(path, quick=False):
    plan = json.loads(path.read_text())
    if plan.get('tie_rule', 'first-seen') not in ['first-seen', 'random-own']:
        raise ValueError('paper comparison supports first-seen or random-own')
    if quick:
        plan.update(alphas=[.3, .6], delays=[.01, 1], small_factors=[1, 1.9],
                    seeds=plan['seeds'][:2], end_time=20000, burn_time=1000,
                    lookahead_time=1000,
                    fine_check={'alphas': [.3], 'delays': [1], 'epsilon': .0001})
    if not isinstance(plan['nodes'], int) or plan['nodes'] < 3:
        raise ValueError('integer nodes >= 3 required')
    for key in ['alphas', 'delays', 'small_factors', 'seeds']:
        if not plan[key] or len(set(plan[key])) != len(plan[key]):
            raise ValueError(f'{key} must be nonempty and distinct')
    if len(plan['seeds']) < 2 or any(not isinstance(s, int) or s < 0 for s in plan['seeds']):
        raise ValueError('at least two distinct nonnegative integer seeds required')
    if any(not math.isfinite(a) or not 1/plan['nodes'] < a < 1 for a in plan['alphas']):
        raise ValueError('A must be the largest miner and 0 < alpha < 1')
    for key in ['delays', 'small_factors']:
        if any(not math.isfinite(d) or d <= 0 for d in plan[key]):
            raise ValueError('positive finite delays and factors required')
    eps = plan['epsilon']
    fine = plan.get('fine_check', {})
    fine_eps = fine.get('epsilon', eps)
    if not all(math.isfinite(e) and 0 < e < 1 for e in [eps, fine_eps]):
        raise ValueError('0 < epsilon < 1 required')
    if fine and (not set(fine['alphas']) <= set(plan['alphas'])
                 or not set(fine['delays']) <= set(plan['delays']) or fine_eps >= eps):
        raise ValueError('fine check must select grid cells and use a smaller step')
    # In this grouped matrix, the only nontrivial triangle is O->A->O.
    if max(plan['small_factors']) > 2-eps:
        raise ValueError('interventions must preserve the propagation triangle inequality')
    if not all(math.isfinite(plan[k]) and plan[k] >= 0
               for k in ['end_time', 'burn_time', 'lookahead_time']):
        raise ValueError('invalid observation times')
    if plan['end_time'] <= plan['burn_time'] or plan['lookahead_time'] <= 0:
        raise ValueError('positive observation and continuation windows required')
    return plan


def cases(plan):
    fine = plan.get('fine_check', {})
    for a in plan['alphas']:
        for d in plan['delays']:
            for factor in plan['small_factors']:
                steps = [plan['epsilon']]
                if a in fine.get('alphas', []) and d in fine.get('delays', []):
                    steps.append(fine['epsilon'])
                for seed in plan['seeds']:
                    yield a, d, factor, 'baseline', 0, seed
                    for eps in steps:
                        for direction in ['send', 'receive']:
                            yield a, d, factor, direction, eps, seed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT/'experiments/plans/paper_comparison.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    if args.workers < 1:
        parser.error('positive worker count required')
    plan = load_plan(args.plan, args.quick)
    args.output.mkdir(parents=True, exist_ok=True)
    if any((args.output/name).exists() for name in ['runs.jsonl', 'plan.json']):
        parser.error('output already exists; choose a new directory')
    rule = plan.get('tie_rule', 'first-seen')
    binary = ROOT/'build'/('latency_simulator' if rule == 'first-seen' else 'tie_simulator')
    source = ROOT/'src'/('latency_simulator.cpp' if rule == 'first-seen' else 'tie_simulator.cpp')
    subprocess.run(['make', str(binary.relative_to(ROOT))], cwd=ROOT, check=True)
    (args.output/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    cc = list(cases(plan))

    def run(case):
        a, d, factor, direction, eps, seed = case
        send = d*(1-eps) if direction == 'send' else d
        receive = d*(1-eps) if direction == 'receive' else d
        command = [binary, *([] if rule == 'first-seen' else [rule]), plan['nodes'], a, send, receive,
                   factor*d, plan['end_time'], plan['burn_time'], plan['lookahead_time'], seed]
        result = subprocess.run(list(map(str, command)), check=True, capture_output=True, text=True)
        row = json.loads(result.stdout)
        row.update(baseline_delay=d, small_factor=factor, direction=direction,
                   epsilon=eps, kind='baseline' if direction == 'baseline' else 'improve')
        if row['accepted_A'] > row['generated_A'] or row['accepted_A'] > row['accepted_all']:
            raise RuntimeError('invalid reward counts')
        if row['confirmation_blocks_reorganized']:
            raise RuntimeError('unstable measured prefix; increase continuation time')
        return row

    started = time.monotonic()
    with (args.output/'runs.jsonl').open('w') as out:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            for index, row in enumerate(pool.map(run, cc), 1):
                out.write(json.dumps(row)+'\n')
                if index % 48 == 0:
                    out.flush()
                    print(f'{index}/{len(cc)} runs; {time.monotonic()-started:.1f}s', flush=True)
    manifest = dict(runs=len(cc), elapsed_seconds=time.monotonic()-started, quick=args.quick,
                    model=f'Finite miners, constant-rate Poisson, {rule}, direct fixed delays, T=1',
                    python=platform.python_version(), platform=platform.platform(),
                    compiler=subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
                    triangle_inequality='Preserved at baseline and at both unilateral interventions',
                    prefix_check='Two finite continuations: empirical stability, not a finality theorem')
    for name, path in [('source', source), ('included_first_seen_source', ROOT/'src/latency_simulator.cpp'),
                       ('binary', binary), ('runner', Path(__file__))]:
        manifest[name+'_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(f'Completed {len(cc)} runs: {args.output}', flush=True)


if __name__ == '__main__':
    main()
