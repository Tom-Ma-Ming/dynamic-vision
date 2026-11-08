"""Audit incremental simulator command causality; exits nonzero on invalid evidence."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path


PENDING_END = {'dispatch_failed', 'rejected', 'cancelled_in_transit',
               'disconnected_in_transit', 'window_end_pending'}
ACTIVE_END = {'expired', 'replaced', 'cancelled', 'disconnected', 'window_end_active'}
IDENTITY = ('frame_id', 'track_id', 'generated', 'arrival', 'expires')


def audit(rows):
    states = {}
    counts = Counter()
    for index, row in enumerate(rows, 1):
        key = (row['scene'], row['id'])
        kind = row['kind']
        def require(condition, message):
            if not condition:
                raise ValueError(f'event {index}, command {key}: {message}')
        require(all(math.isfinite(row[k]) for k in ('dx', 'dy')), 'nonfinite movement')
        require(row['generated'] <= row['arrival'] <= row['expires'], 'invalid lifetime')
        if kind == 'generated':
            require(key not in states, 'duplicate generation')
            require(row['timestamp'] == row['generated'], 'generation timestamp mismatch')
            states[key] = {'identity': tuple(row[k] for k in IDENTITY),
                           'state': 'pending', 'time': row['timestamp']}
        else:
            require(key in states, 'event without generation')
            state = states[key]
            require(tuple(row[k] for k in IDENTITY) == state['identity'], 'identity changed')
            require(row['timestamp'] >= state['time'], 'command time reversed')
            require(state['state'] != 'terminal', 'event after terminal')
            if kind == 'accepted':
                require(state['state'] == 'pending', 'duplicate acceptance')
                require(row['arrival'] <= row['timestamp'] <= row['expires'], 'acceptance outside lifetime')
                state['state'] = 'active'
            elif kind == 'applied':
                require(state['state'] == 'active', 'movement before acceptance')
                require(row['timestamp'] >= row['arrival'], 'movement before arrival')
                # An advance aggregates the preceding interval, so its reporting
                # timestamp may exceed expiry; do not infer a hardware step here.
            elif kind in PENDING_END | ACTIVE_END:
                require(state['state'] == ('pending' if kind in PENDING_END else 'active'),
                        'terminal incompatible with command state')
                if kind == 'expired':
                    require(row['timestamp'] >= row['expires'], 'expiry reported before deadline')
                state['state'] = 'terminal'
            else:
                require(False, f'unknown event {kind}')
            state['time'] = row['timestamp']
        counts[kind] += 1
    if not states:
        raise ValueError('empty command evidence')
    unfinished = [key for key, state in states.items() if state['state'] != 'terminal']
    if unfinished:
        raise ValueError(f'unfinished commands: {unfinished[:10]}')
    return {'commands': len(states), 'event_counts': dict(counts),
            'exactly_one_terminal_per_command': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('commands', type=Path)
    parser.add_argument('--repeat', type=Path, help='Require byte-identical deterministic repeat')
    args = parser.parse_args()
    result = audit(json.loads(line) for line in args.commands.read_text(encoding='utf-8').splitlines())
    if args.repeat:
        if args.commands.read_bytes() != args.repeat.read_bytes():
            raise ValueError('repeat command trace differs')
        result['repeat_exact'] = True
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
