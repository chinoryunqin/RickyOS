"""Read-only numeric summary of a private automatic-test log; never emits raw lines."""
import argparse
import json
from pathlib import Path
import re
import statistics


def parse(text):
    rounds, current = [], None
    final = None
    body_rounds, body_final = [], None
    body_transfers, weread_probes, weread_final = [], [], None
    last_layers = {}
    last_memory = {}
    for line in text.splitlines():
        if '[ACCEPT]' in line and ('name=font12 result=' in line or 'weread_probe ' in line):
            numbers = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
            if 'name=font12 result=' in line:
                body_transfers.append({key: numbers[key] for key in
                    ('result','download_ms','bytes','crc32','content_match','cleanup','only_cos')})
            else:
                stage = re.search(r'\bstage=(shelf|detail|cache)\b', line)
                numbers['stage'] = stage[1] if stage else 'session-check'
                weread_probes.append(numbers)
            continue
        if 'NETALLOC:' in line:
            values = dict(re.findall(r'(\w[\w-]*)=([\w-]+)', line))
            if 'layer' in values:
                layer = values['layer']
                numbers = {key: int(values[key]) for key in ('calls', 'failed', 'nomem', 'max_failed_bytes', 'last')}
                last_layers[layer] = numbers
                if current is not None and layer not in current['before']:
                    current['before'][layer] = numbers.copy()
            elif 'internal_free' in values:
                last_memory = {key: int(values[key]) for key in ('internal_free', 'internal_largest', 'dma_free', 'dma_largest')}
            continue
        if '[FONTNETAUTO]' not in line:
            continue
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
        if ' weread_done ' in line:
            weread_final = {key: values[key] for key in ('attempted', 'passed', 'needs_login', 'expected')}
        elif ' body_done ' in line:
            body_final = {key: values[key] for key in ('attempted', 'passed', 'expected')}
        elif ' font_body ' in line:
            body_rounds.append({'round': values['round'], 'ok': bool(values['ok'])})
        elif 'joined' in values:
            current = {'round': values['round'], 'phase': values['phase'], 'copy_tx': bool(values['copy_tx']),
                       'joined': bool(values['joined']), 'join_ms': values['join_ms'], 'before': {}}
            rounds.append(current)
        elif 'ok' in values and current is not None:
            if current['round'] != values['round'] or current['phase'] != values['phase']:
                raise ValueError('Mismatched round boundaries')
            current.update({key: values[key] for key in ('ok', 'cleanup', 'request_ms', 'families', 'files', 'clock_started')})
            current['after'] = {key: value.copy() for key, value in last_layers.items()}
            current['memory_after'] = last_memory.copy()
            current['delta'] = {}
            for layer, before in current['before'].items():
                after = current['after'].get(layer)
                if after:
                    current['delta'][layer] = {key: after[key] - before[key] for key in ('calls', 'failed', 'nomem')}
            current = None
        elif 'attempted' in values:
            final = {key: values[key] for key in ('attempted', 'passed', 'expected', 'elapsed_ms', 'auto_reconnect_restored')}
    groups = []
    for phase in range(3):
        complete = [row for row in rounds if row['phase'] == phase and 'ok' in row]
        success = [row for row in complete if row['ok'] and row['cleanup']]
        groups.append({'phase': phase, 'mode': 'copy' if phase == 1 else 'reference', 'completed': len(complete),
                       'success': len(success), 'failure': len(complete) - len(success),
                       'success_request_median_ms': statistics.median(row['request_ms'] for row in success) if success else None,
                       'wifi_reference_nomem': sum(row['delta'].get('wifi-ref', {}).get('nomem', 0) for row in complete),
                       'wifi_copy_nomem': sum(row['delta'].get('wifi-copy', {}).get('nomem', 0) for row in complete),
                       'pbuf_allocation_failures': sum(row['delta'].get('pbuf', {}).get('failed', 0) for row in complete)})
    return {'schema': 'plus-netauto-summary/1', 'samples': rounds, 'groups': groups, 'final': final,
            'font_body_samples': body_rounds, 'font_body_final': body_final,
            'font_body_transfers': body_transfers, 'weread_probes': weread_probes, 'weread_final': weread_final,
            'completed_all_rounds': bool(final and final['attempted'] == final['expected'] == 12 and len([r for r in rounds if 'ok' in r]) == 12),
            'limitations': ['core download/parser lifecycle, not full UI navigation', 'small ordered sample, not a population failure rate',
                            'driver accepts a packet is not proof of remote delivery', 'not sustained throughput or CPU/power upper bound']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--full-ui', action='store_true', help='Label corrected Settings/FontLibrary navigation runs')
    args = parser.parse_args()
    result = parse(args.log.read_text(errors='replace'))
    if args.full_ui:
        result['limitations'][0] = 'real Settings/FontLibrary/FontDownload lifecycle and display; automated navigation differs from human timing'
        result['profile'] = 'full-parent-ui-with-drained-transitions'
    if args.output:
        with args.output.open('x') as out:
            json.dump(result, out, indent=2)
    print(json.dumps({'groups': result['groups'], 'final': result['final'], 'completed_all_rounds': result['completed_all_rounds'],
                      'font_body_final': result['font_body_final'], 'font_body_transfers': result['font_body_transfers'],
                      'weread_final': result['weread_final'], 'weread_probes': result['weread_probes']}))
