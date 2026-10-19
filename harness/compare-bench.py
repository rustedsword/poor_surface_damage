#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare two outputs of damage-bench: how many times fewer cycles NEW takes than OLD, and how much more it redraws."""

import math
import sys
from collections import defaultdict

KEY = ('tracker', 'work', 'order', 'buffers', 'rects')


def load(path):
    with open(path) as file:
        header, *rows = (line.split() for line in file if line.strip())
    return {tuple(row[header.index(column)] for column in KEY): dict(zip(header, row)) for row in rows}


def value(row, column):
    return float(row[column].rstrip('%'))


def geomean(values):
    return math.exp(sum(map(math.log, values)) / len(values))


if len(sys.argv) != 3:
    sys.exit(f'Usage: {sys.argv[0]} OLD NEW')
old, new = load(sys.argv[1]), load(sys.argv[2])
groups = defaultdict(list)
for key in old.keys() & new.keys():
    groups[key[0], key[1], int(key[4])].append((value(old[key], 'cycles') / value(new[key], 'cycles'),
                                               value(new[key], 'redrawn') / value(old[key], 'redrawn')))
print(f"{'tracker':8} {'work':9} {'rects':>5} {'speedup':>8} {'min':>6} {'max':>6} {'redrawn':>8}")
for (tracker, work, rects), runs in sorted(groups.items()):
    speedups = [speedup for speedup, _ in runs]
    print(f'{tracker:8} {work:9} {rects:5} {geomean(speedups):8.2f} {min(speedups):6.2f} {max(speedups):6.2f}'
          f' {max(redrawn for _, redrawn in runs):8.3f}')
print(f'overall {geomean([speedup for runs in groups.values() for speedup, _ in runs]):.2f}')
