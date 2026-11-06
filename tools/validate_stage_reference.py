#!/usr/bin/env python3
"""Private, optional comparison of stage extraction with original constant setup calls.

Original instructions execute only in the existing isolated argument-capture harness.
All called engine routines are RET stubs. No Windows APIs, files or authentication run.
This validates registration data, not the complete original game's story behaviour.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
from validate_navigation_reference import capture, ARITIES


def validate(executable, model):
    arities = dict(ARITIES)
    arities.update({0x429700: 4, 0x429ac0: 9, 0x429c20: 9, 0x429be0: 3,
                    0x42a000: 1, 0x42a080: 9, 0x42a170: 12, 0x42a270: 4,
                    0x42a390: 7, 0x42a5c0: 4, 0x42a610: 4})
    calls, read = capture(executable, 0x47cea0, 0x47f01b, arities)
    def name(address):
        raw = bytearray()
        for i in range(96):
            c = read(address+i, 1)
            if c == b'\0': return raw.decode('ascii').upper()
            raw += c
        raise ValueError('unbounded string')
    def signed(v): return v if v < 2**31 else v-2**32
    def real(v): return struct.unpack('<f', struct.pack('<I', v))[0]
    rows = model.splitlines()
    if rows.pop(0) != 'JERUSALEM_A01_STAGE_V1': raise ValueError('unsupported stage model')
    actual = Counter(tuple(line.split()) for line in rows if line and not line.startswith('dialogue '))
    expected = Counter()
    indices = Counter()
    for target, a in calls:
        row = None
        if target == 0x42af00:
            row = ('background', a[0], name(a[1]), signed(a[2]), signed(a[3]))
        elif target in (0x429ac0, 0x429c20):
            row = ('access', a[0], a[1], indices[a[0]], int(target == 0x429c20), a[6],
                   *map(signed, a[2:6]), a[7], a[8])
            indices[a[0]] += 1
        elif target in (0x42a270, 0x42a390):
            animated = target == 0x42a390
            row = ('layer', a[0], a[1], a[2], 1, int(animated), '-', a[3],
                   a[4] if animated else 1, a[6] if animated else 0, 1000, 0, 0,
                   format(real(a[5]), '.12g') if animated else 0)
        elif target in (0x42a080, 0x42a170):
            animated = target == 0x42a170
            row = ('layer', a[0], a[1], a[2], 0, int(animated), name(a[3]),
                   a[4] if animated else 0, a[9] if animated else 1,
                   a[11] if animated else 0, a[8], signed(a[5] if animated else a[4]),
                   signed(a[6] if animated else a[5]), format(real(a[10]), '.12g') if animated else 0)
        if row: expected[tuple(map(str, row))] += 1
    if actual != expected:
        raise ValueError(f'stage records differ: missing={list((expected-actual).items())[:3]}, extra={list((actual-expected).items())[:3]}')
    return dict(captured_setup_calls=len(calls), verified_stage_records=sum(expected.values()), matches=True,
                scope='backgrounds, pixel/angular hotspots, static/animated layers; voices and story callbacks require separate checks')


def validate_escape_effects(executable):
    # Trace only the selected handler branches. Their host APIs remain capture stubs.
    import validate_story_reference as original
    original.ARITIES.update({0x42ab20: 1, 0x42aaa0: 1, 0x429d90: 3, 0x42c6e0: 0,
                             0x4282a0: 2, 0x42c550: 1, 0x42c670: 2, 0x4292c0: 3,
                             0x44f490: 1, 0x42cdd0: 1})
    _, read = capture(executable)
    def media(calls):
        result = []
        for address, args in calls:
            if address == 0x4282a0:
                raw = read(args[0], 32).split(b'\0', 1)[0].decode('ascii')
                result.append(('movie', raw))
            elif address == 0x4292c0: result.append(('puzzle', args[0]))
            elif address == 0x42ccd0: result.append(('speech', args[0]))
        return result
    cases = 0
    for character in (2, 3):
        for owned in ((10004,), (10017,)):
            calls = original.trace(read, 10011, 10001, {10000: 2, 10003: character}, True, owned=owned)
            if media(calls) != [('movie', 'A01S01_AN004'), ('puzzle', 10000)]:
                raise ValueError('original palm escape differs')
            cases += 1
    for item in (10004, 10017):
        calls = original.trace(read, 10005, item, {}, True)
        if media(calls) != [('movie', 'A01S01_AN007C'), ('puzzle', 10020), ('speech', 11011)]:
            raise ValueError('original stone/sling success differs')
        cases += 1
    for target, movie in ((10012, 'A01S01_AN007A'), (10013, 'A01S01_AN007B')):
        calls = original.trace(read, target, 10017, {}, True)
        if media(calls) != [('movie', movie), ('puzzle', 10044)]:
            raise ValueError('original wrong-target failure differs')
        cases += 1
    return dict(escape_handler_cases=cases, matches=True,
                scope='original media/puzzle/speech requests for palm escape, both successful projectiles, and both wrong targets')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path); parser.add_argument('model', type=Path)
    args = parser.parse_args()
    result = validate(args.executable.read_bytes(), args.model.read_text())
    result['escape'] = validate_escape_effects(args.executable.read_bytes())
    print(json.dumps(result, indent=2))
