#!/usr/bin/env python3
"""Plan statistics and consistency check for docs/falloutmp (QA-001).

Prints task counts per workstream/feature, size sums, Accept/Files coverage,
duplicate IDs and references to undefined task IDs. Exit code 1 on duplicates
or undefined references.
"""
import re, sys, glob, os, collections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'falloutmp')
TASK_RE = re.compile(r"^- \[[ x\-~]\] \*\*([A-Z]+-\d{3}[a-z]?|F\d{2}-T\d{2})\*\*(.*)$", re.M)
ID_RE = re.compile(r"\b([A-Z]{2,5}-\d{3}[a-z]?|F\d{2}-T\d{2})\b")
SIZE_RE = re.compile(r" — (S|M|L|XL)(?= — |$| \()|^  - (S|M|L|XL) — ", re.M)
MID = {'S': 0.2, 'M': 1.0, 'L': 4.0, 'XL': 8.0}  # weeks

defs = collections.defaultdict(list)
sizes = {}
accept = set(); files = set()
texts = {}
for path in sorted(glob.glob(os.path.join(ROOT, '**', '*.md'), recursive=True)):
    s = open(path, encoding='utf-8').read()
    texts[path] = s
    lines = s.split('\n')
    for i, line in enumerate(lines):
        m = TASK_RE.match(line)
        if not m:
            continue
        tid, rest = m.group(1), m.group(2)
        defs[tid].append(os.path.relpath(path, ROOT))
        body = rest
        j = i + 1
        while j < len(lines) and lines[j].startswith('  - '):
            body += '\n' + lines[j]
            j += 1
        sm = SIZE_RE.search(body)
        if sm:
            sizes[tid] = sm.group(1) or sm.group(2)
        if 'Files:' in body:
            files.add(tid)
        if 'Accept:' in body:
            accept.add(tid)
        if 'tracked in F02' in rest:
            sizes[tid] = 'S'

dups = {k: v for k, v in defs.items() if len(v) > 1}
all_ids = set(defs)
refs = collections.defaultdict(set)
for path, s in texts.items():
    for m in ID_RE.finditer(s):
        refs[m.group(1)].add(os.path.relpath(path, ROOT))
skip_prefixes = ('ADR-', 'I', 'Q-', 'R', 'S', 'ESL', 'MQ', 'DLC', 'UTF')
undefined = {k: v for k, v in refs.items() if k not in all_ids and not k.startswith(skip_prefixes)}

by_ws = collections.Counter(re.match(r"(F\d{2}|[A-Z]+)", t).group(1) for t in all_ids)
feat = sum(v for k, v in by_ws.items() if re.fullmatch(r'F\d{2}', k))
infra = len(all_ids) - feat
size_count = collections.Counter(sizes.values())
weeks_mid = sum(MID.get(sizes.get(t, 'M'), 1.0) for t in all_ids)

print(f"tasks: {len(all_ids)} (feature {feat}, infra {infra})")
print("by size:", dict(size_count), f"| unsized: {len(all_ids) - len(sizes)}")
print(f"sum of size midpoints: {weeks_mid:.0f} developer-weeks")
print(f"Accept lines: {len(accept)}/{len(all_ids)}  Files lines: {len(files)}/{len(all_ids)}")
print("by workstream:", ', '.join(f"{k} {v}" for k, v in sorted(by_ws.items())))
if dups:
    print("DUPLICATE IDS:", dups)
if undefined:
    print("UNDEFINED REFERENCES:", {k: sorted(v) for k, v in sorted(undefined.items())})
sys.exit(1 if dups or undefined else 0)
