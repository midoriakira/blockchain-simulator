"""Analyze paired improvements; report finite differences, not exact derivatives."""
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
import statistics

from scipy.stats import t


def interval(values):
    mean = statistics.mean(values)
    sd = statistics.stdev(values) if len(values) > 1 else 0
    if sd == 0:
        return mean, None, None
    half = float(t.ppf(.975, len(values)-1))*sd/math.sqrt(len(values))
    return mean, mean-half, mean+half


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--plots', action='store_true')
    args = parser.parse_args()
    plan = json.loads((args.directory/'plan.json').read_text())
    rows = [json.loads(line) for line in (args.directory/'runs.jsonl').read_text().splitlines()]
    lookup = {(r['small_delay'], r['direction'], r['epsilon'], r['kind'], r['seed']): r for r in rows}
    if len(lookup) != len(rows):
        raise ValueError('duplicate run keys')
    groups = defaultdict(list)
    for row in rows:
        if row['kind'] != 'baseline':
            groups[row['small_delay'], row['direction'], row['epsilon'], row['kind']].append(row)
    output = []
    for (small, direction, epsilon, kind), rr in sorted(groups.items()):
        metrics = defaultdict(list)
        nonzero = 0
        if {r['seed'] for r in rr} != set(plan['seeds']):
            raise ValueError('incomplete seed group')
        for row in rr:
            base = lookup[small, 'baseline', 0, 'baseline', row['seed']]
            if row['generated_A'] != base['generated_A']:
                raise ValueError('paired generation counts differ')
            if not base['accepted_A'] or base['reward_share_A'] is None or row['reward_share_A'] is None:
                raise ValueError('empty reward sample; increase observation time')
            delta = row['accepted_A']-base['accepted_A']
            nonzero += delta != 0
            sign = 1 if kind == 'improve' else -1
            fraction = sign*delta/base['accepted_A']
            metrics['effect_percent'].append(100*fraction)
            metrics['income_elasticity'].append(fraction/epsilon)
            metrics['share_pp_per_1pct'].append(sign*(row['reward_share_A']-base['reward_share_A'])/epsilon)
        record = dict(small_delay=small, direction=direction, epsilon=epsilon, kind=kind,
                      seeds=len(rr), nonzero_income_pairs=nonzero)
        for name, values in metrics.items():
            for suffix, value in zip(['mean', 'ci95_low', 'ci95_high'], interval(values)):
                record[name+'_'+suffix] = value
        output.append(record)
    if not output:
        raise ValueError('no paired interventions')
    with (args.directory/'paired_effects.csv').open('w', newline='', encoding='utf-8-sig') as out:
        writer = csv.DictWriter(out, fieldnames=list(output[0]))
        writer.writeheader()
        writer.writerows(output)
    summary = {'runs': len(rows), 'conditions': len(output),
               'inference': 'Pointwise approximate 95% Student-t intervals across paired seeds.',
               'zero_variance': 'A missing interval means no observed variation, not zero uncertainty.',
               'income_elasticity': 'Fractional income effect divided by fractional latency change.',
               'share_pp_per_1pct': 'Share change in percentage points per 1% latency change.',
               'interpretation': 'Finite one-sided differences; do not assume a derivative at a boundary.',
               'effects': output}
    (args.directory/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    if args.plots:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
        for ax, metric, label in [(axes[0], 'income_elasticity', 'Income elasticity'),
                                  (axes[1], 'share_pp_per_1pct', 'Share sensitivity (pp per 1%)')]:
            for direction in ['send', 'receive', 'both']:
                for epsilon in sorted(plan['epsilons']):
                    selected = [r for r in output if r['direction'] == direction
                                and r['epsilon'] == epsilon and r['kind'] == 'improve']
                    if not selected:
                        continue
                    x = [r['small_delay'] for r in selected]
                    y = [r[metric+'_mean'] for r in selected]
                    line, = ax.plot(x, y, 'o', linestyle='none', ms=3, label=f'{direction}, {epsilon*100:g}%')
                    for r in selected:
                        lo, hi = r[metric+'_ci95_low'], r[metric+'_ci95_high']
                        if lo is not None:
                            ax.errorbar(r['small_delay'], r[metric+'_mean'],
                                        yerr=[[r[metric+'_mean']-lo], [hi-r[metric+'_mean']]],
                                        color=line.get_color(), capsize=2)
            ax.axhline(0, color='gray', lw=.8)
            ax.set(xlabel='Small-to-small delay / T', ylabel=label)
            ax.grid(alpha=.2)
            ax.legend(fontsize=6)
        fig.suptitle('One-sided finite differences; missing intervals do not imply zero uncertainty', fontsize=10)
        fig.tight_layout()
        fig.savefig(args.directory/'effects.pdf')
        fig.savefig(args.directory/'effects.png', dpi=160)
    print(f'Analyzed {len(rows)} runs, {len(output)} intervention conditions.')


if __name__ == '__main__':
    main()
