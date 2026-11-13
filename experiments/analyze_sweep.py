"""Compare paired infinitesimal delay improvements with arXiv:2606.06860.

K = Delta MPR / (epsilon * baseline_delay/T). First-seen and the
own-preferring random rule have separate first-order predictions.
"""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

import scipy
from scipy.stats import t


def paper_K(plan, a):
    rule = plan.get('tie_rule', 'first-seen')
    if rule == 'first-seen':
        return (1-a)*(1-2*a)
    if rule == 'random-own':
        return .5*((1-a)*(1-3*a)+(1-a)**2/(plan['nodes']-1))
    raise ValueError('no paper prediction specified for this tie rule')


def interval(values):
    mean = statistics.mean(values)
    sd = statistics.stdev(values)
    if sd == 0:
        return mean, None, None
    half = float(t.ppf(.975, len(values)-1))*sd/math.sqrt(len(values))
    return mean, mean-half, mean+half


def pvalue(values, null=0):
    sd = statistics.stdev(values)
    if sd == 0:
        return None
    z = (statistics.mean(values)-null)/(sd/math.sqrt(len(values)))
    return float(2*t.sf(abs(z), len(values)-1))


def holm(records, field, output):
    ordered = sorted((r for r in records if r[field] is not None), key=lambda r: r[field])
    # Include uninformative/degenerate comparisons in the family size.
    count = len(records)
    last = 0
    for index, row in enumerate(ordered):
        last = max(last, min(1., (count-index)*row[field]))
        row[output] = last
    for row in records:
        row.setdefault(output, None)


def write_csv(path, rows):
    if not rows:
        raise ValueError('empty analysis')
    with path.open('w', newline='', encoding='utf-8-sig') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def analyze(directory):
    plan = json.loads((directory/'plan.json').read_text())
    rows = [json.loads(s) for s in (directory/'runs.jsonl').read_text().splitlines()]
    lookup = {(r['alpha'], r['baseline_delay'], r['small_factor'], r['direction'],
               r['epsilon'], r['seed']): r for r in rows}
    if len(lookup) != len(rows):
        raise ValueError('duplicate simulation keys')
    samples, baselines = defaultdict(list), defaultdict(list)
    paired = []
    for row in rows:
        a, d, factor, seed = (row[k] for k in ['alpha', 'baseline_delay', 'small_factor', 'seed'])
        if row['confirmation_blocks_reorganized'] or row['reward_share_A'] is None:
            raise ValueError('unstable or empty observation window')
        if row['kind'] == 'baseline':
            baselines[a, d, factor].append(row)
            continue
        eps, direction = row['epsilon'], row['direction']
        base = lookup[a, d, factor, 'baseline', 0, seed]
        if row['generated_A'] != base['generated_A'] or base['accepted_A'] == 0:
            raise ValueError('invalid paired generation or empty A reward sample')
        if not math.isclose(row['small_delay'], factor*d) or row['small_delay'] > row['out_delay']+row['in_delay']:
            raise ValueError('delay intervention violates its declared matrix')
        dr = row['reward_share_A']-base['reward_share_A']
        metric = dict(alpha=a, baseline_delay=d, small_factor=factor,
                      direction=direction, epsilon=eps, seed=seed,
                      delta_share=dr, delta_MPR=dr/a, K=dr/(a*eps*d),
                      share_pp_per_1pct=dr/eps,
                      income_elasticity=(row['accepted_A']-base['accepted_A'])/(eps*base['accepted_A']))
        paired.append(metric)
        samples[a, d, factor, direction, eps].append(metric)
    # Average directions within each seed. This is still n independent seeds,
    # and must never be treated as twice as many independent replications.
    for (a, d, factor, direction, eps), ss in list(samples.items()):
        if {r['seed'] for r in ss} != set(plan['seeds']):
            raise ValueError('incomplete paired seed group')
        if direction != 'send':
            continue
        other = {r['seed']: r for r in samples[a, d, factor, 'receive', eps]}
        for s in ss:
            avg = dict(s, direction='average')
            for k in ['delta_share', 'delta_MPR', 'K', 'share_pp_per_1pct', 'income_elasticity']:
                avg[k] = (s[k]+other[s['seed']][k])/2
            samples[a, d, factor, 'average', eps].append(avg)
    results = []
    for (a, d, factor, direction, eps), ss in sorted(samples.items()):
        theory = paper_K(plan, a)
        r = dict(alpha=a, baseline_delay=d, small_factor=factor, direction=direction,
                 epsilon=eps, seeds=len(ss), nonzero_share_pairs=sum(s['delta_share'] != 0 for s in ss),
                 paper_K=theory, paper_share_pp_per_1pct=a*d*theory,
                 p_zero=pvalue([s['K'] for s in ss]),
                 p_paper=pvalue([s['K'] for s in ss], theory))
        for name in ['K', 'delta_share', 'delta_MPR', 'share_pp_per_1pct', 'income_elasticity']:
            for suffix, value in zip(['mean', 'ci95_low', 'ci95_high'], interval([s[name] for s in ss])):
                r[name+'_'+suffix] = value
        results.append(r)
    # Separate families for the primary directional tests and the within-seed
    # direction averages. Fine-step tests are a further, separately marked family.
    for primary in [True, False]:
        for average in [True, False]:
            family = [r for r in results if (r['epsilon'] == plan['epsilon']) == primary
                      and (r['direction'] == 'average') == average]
            for r in family:
                r['holm_family_size'] = len(family)
                r['holm_family'] = ('primary' if primary else 'fine')+('_average' if average else '_directional')
            holm(family, 'p_zero', 'p_zero_holm')
            holm(family, 'p_paper', 'p_paper_holm')
    baseline_results = []
    for (a, d, factor), rr in sorted(baselines.items()):
        if {r['seed'] for r in rr} != set(plan['seeds']):
            raise ValueError('incomplete baseline seeds')
        q = 1-a
        if plan.get('tie_rule', 'first-seen') == 'first-seen':
            prediction = q*(2*a-1)*2*d+2*q*q*(1-1/(plan['nodes']-1))*factor*d
        else:
            s2 = q*q/(plan['nodes']-1)
            prediction = (a*q-.5*(q*q+s2))*2*d+(q*q-s2)*factor*d
        rec = dict(alpha=a, baseline_delay=d, small_factor=factor, seeds=len(rr), paper_MPR=prediction)
        for name, values in [('MPR', [(r['reward_share_A']-a)/a for r in rr]),
                             ('chain_rate', [r['chain_rate'] for r in rr])]:
            for suffix, value in zip(['mean', 'ci95_low', 'ci95_high'], interval(values)):
                rec[name+'_'+suffix] = value
        baseline_results.append(rec)
    stability = []
    primary_eps = plan['epsilon']
    for (a, d, factor, direction, eps), ss in sorted(samples.items()):
        if eps == primary_eps:
            continue
        coarse = {r['seed']: r for r in samples[a, d, factor, direction, primary_eps]}
        diff = [r['K']-coarse[r['seed']]['K'] for r in ss]
        mean, lo, hi = interval(diff)
        stability.append(dict(alpha=a, baseline_delay=d, small_factor=factor, direction=direction,
                              coarse_epsilon=primary_eps, fine_epsilon=eps, seeds=len(ss),
                              K_fine_minus_coarse_mean=mean, ci95_low=lo, ci95_high=hi,
                              p_zero=pvalue(diff)))
    holm(stability, 'p_zero', 'p_zero_holm')
    write_csv(directory/'paired_seed_effects.csv', paired)
    write_csv(directory/'effects.csv', results)
    write_csv(directory/'baselines.csv', baseline_results)
    write_csv(directory/'step_stability.csv', stability)
    summary = dict(runs=len(rows), cells=len(baselines), conditions=len(results),
                   analysis_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   scipy_version=scipy.__version__,
                   source='https://arxiv.org/html/2606.06860v1',
                   metric='MPR=(reward_share_A-alpha)/alpha; K=Delta_MPR/(epsilon*d/T)',
                   tie_rule=plan.get('tie_rule', 'first-seen'),
                   paper_prediction='FS: q*(1-2*alpha); random-own: (q*(1-3*alpha)+sum(beta_j^2))/2. Both are first-order small-delay approximations.',
                   inference='Pointwise approximate 95% Student-t intervals over independent paired seeds; Holm p-values for separate declared families.',
                   zero_variance='Missing intervals/p-values mean no observed variation, not zero uncertainty.',
                   averaging='send and receive averaged WITHIN each seed; independent sample size remains the seed count.',
                   interpretation='One-sided finite differences; high delay extends beyond paper Cond. A, while Cond. B is retained.',
                   effects=results, baselines=baseline_results, step_stability=stability)
    (directory/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    return plan, results


def plots(directory, plan, results):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    colors = plt.cm.viridis(np.linspace(.08, .92, len(plan['alphas'])))
    fig, axes = plt.subplots(1, len(plan['small_factors']), figsize=(12, 4.7), squeeze=False)
    for ax, factor in zip(axes[0], plan['small_factors']):
        for a, color in zip(plan['alphas'], colors):
            rr = [r for r in results if r['alpha'] == a and r['small_factor'] == factor
                  and r['direction'] == 'average' and r['epsilon'] == plan['epsilon']]
            ax.plot([r['baseline_delay'] for r in rr], [r['K_mean'] for r in rr],
                    'o', linestyle='none', ms=4, color=color, label=f'alpha={a:g}')
            ax.axhline(paper_K(plan, a), color=color, linestyle='--', lw=.8, alpha=.7)
            for r in rr:
                if r['K_ci95_low'] is not None:
                    ax.errorbar(r['baseline_delay'], r['K_mean'],
                                yerr=[[r['K_mean']-r['K_ci95_low']], [r['K_ci95_high']-r['K_mean']]],
                                color=color, capsize=2)
        ax.set(xscale='log', xlabel='Baseline A delay d / T', ylabel='K = Delta MPR / (epsilon * d/T)',
               title=f'Small-to-small delay = {factor:g} d')
        ax.axhline(0, color='black', lw=.7)
        ax.grid(alpha=.2)
    axes[0, 0].legend(fontsize=8, ncol=2)
    fig.suptitle(f'{plan.get("tie_rule", "first-seen")} incentives: simulation (points) vs small-delay paper (dashed)', fontsize=12)
    fig.text(.5, .01, f'{plan["nodes"]} miners; {plan["epsilon"]*100:g}% improvement; {len(plan["seeds"])} paired seeds; send/receive averaged within seed; pointwise 95% t intervals',
             ha='center', fontsize=8)
    fig.tight_layout(rect=[0, .04, 1, .95])
    for ext in ['pdf', 'png']:
        fig.savefig(directory/f'paper_comparison.{ext}', dpi=180)
    plt.close(fig)
    fig, axes = plt.subplots(len(plan['small_factors']), 2, figsize=(11, 7), squeeze=False)
    primary = [r for r in results if r['epsilon'] == plan['epsilon']]
    limit = max(abs(r['K_mean']) for r in primary)
    for rowindex, factor in enumerate(plan['small_factors']):
        for colindex, direction in enumerate(['send', 'receive']):
            ax = axes[rowindex, colindex]
            rr = {(r['alpha'], r['baseline_delay']): r for r in primary
                  if r['small_factor'] == factor and r['direction'] == direction}
            matrix = np.array([[rr[a, d]['K_mean'] for d in plan['delays']] for a in plan['alphas']])
            im = ax.imshow(matrix, cmap='RdBu', vmin=-limit, vmax=limit, aspect='auto')
            for i, a in enumerate(plan['alphas']):
                for j, d in enumerate(plan['delays']):
                    r = rr[a, d]
                    star = '*' if r['p_zero_holm'] is not None and r['p_zero_holm'] < .05 else ''
                    ax.text(j, i, f'{r["K_mean"]:.2f}{star}', ha='center', va='center', fontsize=8,
                            color='white' if abs(r['K_mean']) > .55*limit else 'black')
            ax.set_xticks(range(len(plan['delays'])), [f'{d:g}' for d in plan['delays']])
            ax.set_yticks(range(len(plan['alphas'])), [f'{a:g}' for a in plan['alphas']])
            ax.set(xlabel='d / T', ylabel='Largest hash share alpha', title=f'{direction}; small-to-small = {factor:g} d')
    fig.colorbar(im, ax=axes.ravel().tolist(), label='K (positive: improvement raises reward share)', fraction=.025)
    fig.suptitle('Direction-specific infinitesimal incentives', fontsize=12)
    fig.text(.5, .01, f'* Holm-adjusted two-sided p < 0.05 across all primary directional conditions; {plan["epsilon"]*100:g}% step',
             ha='center', fontsize=8)
    fig.subplots_adjust(left=.08, right=.86, hspace=.42, bottom=.1, top=.9)
    for ext in ['pdf', 'png']:
        fig.savefig(directory/f'incentive_grid.{ext}', dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--plots', action='store_true')
    args = parser.parse_args()
    plan, results = analyze(args.directory)
    if args.plots:
        plots(args.directory, plan, results)
    print(f'Analyzed {len(results)} direction/step conditions: {args.directory}')


if __name__ == '__main__':
    main()
