# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize the opt-in renderer CSV. CPU submission timings, not GPU timings."""
import csv
import json
import sys

def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    return round(values[min(len(values)-1, int((len(values)-1)*fraction))], 4)

for path in sys.argv[1:]:
    with open(path, newline='') as f:
        rows = list(csv.DictReader(f))
    renders = [r for r in rows if r['event'] == 'render']
    snapshots = [r for r in rows if r['event'] == 'snapshot']
    # Exclude boot/first-use compilation from steady-state summaries; retain
    # raw rows so stalls are still available for diagnosis.
    begin = int(rows[0]['time_ns']) + 30_000_000_000 if rows else 0
    renders = [r for r in renders if int(r['time_ns']) >= begin]
    snapshots = [r for r in snapshots if int(r['time_ns']) >= begin]
    durations = [float(r['cpu_ms']) for r in renders]
    gaps = [(int(b['time_ns'])-int(a['time_ns']))/1e6 for a,b in zip(renders,renders[1:])]
    elapsed = (int(renders[-1]['time_ns'])-int(renders[0]['time_ns']))/1e9 if len(renders)>1 else 0
    print(json.dumps(dict(path=path, frames=len(renders), elapsed_seconds=elapsed,
        attempt_hz=(len(renders)-1)/elapsed if elapsed else None,
        replay_cpu_ms=dict(p50=percentile(durations,.5), p95=percentile(durations,.95), p99=percentile(durations,.99)),
        frame_interval_ms=dict(p50=percentile(gaps,.5),p95=percentile(gaps,.95),p99=percentile(gaps,.99)),
        snapshot_cpu_p95_ms=percentile([float(r['cpu_ms']) for r in snapshots],.95),
        unavailable_buffers=sum(int(r['unavailable_buffers']) for r in renders),
        upload_mib=sum(int(r['upload_bytes']) for r in snapshots)/1048576), indent=2))
