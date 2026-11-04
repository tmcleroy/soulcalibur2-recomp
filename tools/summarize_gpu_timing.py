# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize CPU API durations and asynchronous GPU completion bounds."""
import argparse
import csv
import json
from summarize_latency import stats

def summarize(path, scene=7):
    with open(path,newline="",encoding="utf-8") as f: rows=list(csv.DictReader(f))
    if not rows or rows[-1]["event"]!="dropped": raise ValueError("Incomplete GPU trace")
    if int(rows[-1]["begin_ns"]): raise ValueError("GPU trace overflow")
    frames={}
    for row in rows[:-1]:
        if int(row["scene"])!=scene: continue
        events=frames.setdefault(int(row["frame"]),{})
        if row["event"] in events: raise ValueError("Duplicate frame event")
        events[row["event"]]=row
    if not frames: raise ValueError("No frames in requested scene; enable latency provenance")
    metrics={x:[] for x in ("acquire_cpu","submit_cpu","completion_lower","completion_upper","completion_uncertainty")}
    unavailable=failures=incomplete=0
    for events in frames.values():
        for event,key in (("acquire","acquire_cpu"),("submit","submit_cpu")):
            if event in events:
                r=events[event]; delta=int(r["end_ns"])-int(r["begin_ns"])
                if delta<0: raise ValueError("Non-monotonic API time")
                metrics[key].append(delta/1e6)
        if "acquire" in events: unavailable+=events["acquire"]["ok"]!="1"
        if "fence_overflow" in events: failures+=1
        if "submit" not in events: continue
        submit=events["submit"]
        if submit["ok"]!="1": failures+=1; continue
        if "completion_bounds" not in events: incomplete+=1; continue
        c=events["completion_bounds"]; start=int(submit["begin_ns"])
        lower=int(c["begin_ns"])-start; upper=int(c["end_ns"])-start
        if lower<0 or upper<lower: raise ValueError("Invalid completion bracket")
        metrics["completion_lower"].append(lower/1e6)
        metrics["completion_upper"].append(upper/1e6)
        metrics["completion_uncertainty"].append((upper-lower)/1e6)
    return {"frames":len(frames),"unavailable_swapchains":unavailable,"fence_failures":failures,
            "incomplete_fences":incomplete,**{k:stats(v) for k,v in metrics.items()},
            "scope":"CPU acquire/submit durations and submit-start-to-fence-completion bounds. Includes queueing; not GPU execution duration, display time or input latency."}

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("paths",nargs="+");args=ap.parse_args()
    print(json.dumps([summarize(p) for p in args.paths],indent=2))
