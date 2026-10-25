"""Validate the actual export rewrite and candidate-order equivalence."""
from pathlib import Path
import argparse
import random
import math
from patch_aim_query_order import patch, MARKER

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('file', type=Path)
    args = parser.parse_args()
    original = args.file.read_text(encoding='utf-8-sig')
    assert MARKER not in original, 'Use a fresh unpatched export'
    changed = patch(original)
    assert patch(changed) == changed
    # The rewrite only permutes original lines and adds one comment. No query,
    # comparison, assignment or literal may be altered or silently dropped.
    assert sorted(original.splitlines()) == sorted(
        line for line in changed.splitlines() if MARKER not in line)
    for text in (original, changed):
        method = text[text.index('void CameraFrustumTargeter_Update_m'):]
        method = method[:method.index('// Method Definition Index:')]
        assert method.count('PortalPhysicsV2_Raycast_') == 1
        assert method.count('Physics_RaycastNonAlloc_') == 1
    randomizer = random.Random(48)
    reduced = 0
    # Model query results as fixed per-candidate observations. Filtering does
    # not mutate the world; preserve strict-less tie and NaN rejection rules.
    for trial in range(10000):
        values = []
        for index in range(randomizer.randrange(1, 70)):
            x, y, z = [randomizer.uniform(-2, 3) for _ in range(3)]
            score = math.sqrt((x-.5)**2 + (y-.5)**2 + z*z)
            if index % 17 == 0: score = float('nan')
            if index % 19 == 0: x = .75
            values.append((x, y, z, score, randomizer.random() < .25, randomizer.random() < .35))
        def select(early):
            best, winner, queries = float('inf'), None, 0
            for index, (x, y, z, score, portal, blocked) in enumerate(values):
                def reject():
                    return x > .75 or x < .25 or y > .75 or y < .25 or z < 0 or not score < best
                if early and reject(): continue
                queries += 1
                if portal: continue
                queries += 1
                if blocked: continue
                if not early and reject(): continue
                best, winner = score, index
            return winner, queries
        before, after = select(False), select(True)
        assert before[0] == after[0] and after[1] <= before[1]
        reduced += before[1] - after[1]
    print(f'PASS: exact original blocks retained; idempotent; 10000 candidate lists agree; {reduced} model queries avoided (not a hardware benchmark)')

if __name__ == '__main__': main()
