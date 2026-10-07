#!/usr/bin/env python3
"""Pool2 -> FC input rate from a cnn_top trace (./run_sim.sh writes build/<name>_trace.txt).

Per frame: first and last pool_l2 handshake cycle, how many values, and the valid fraction in that window.
Usage: p2_rate.py build/v100_g0_s100_trace.txt [...]
"""
import re
import sys
from collections import defaultdict

LINE = re.compile(r"^\s*\d+\s+(\d+)\s+P2\s+\d+\s+f(\d+)\s")

for path in sys.argv[1:]:
    frames = defaultdict(list)
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if m:
                frames[int(m.group(2))].append(int(m.group(1)))
    print(path)
    for fr in sorted(frames):
        c = frames[fr]
        span = c[-1] - c[0] + 1
        gaps = [b - a for a, b in zip(c, c[1:])]
        print(f"  frame {fr}: {len(c)} values, cycles {c[0]}..{c[-1]} ({span} cycles), "
              f"valid {100.0 * len(c) / span:.1f}%, max gap {max(gaps, default=0)} cycles")
