# SPDX-License-Identifier: GPL-3.0-or-later
"""Join PresentMon v1 QPC records to a single scripted combat interval."""
import argparse
import collections
import csv
import json
import math
from summarize_latency import stats

def summarize(presentation,latency,qpc_frequency,scene=7):
    if qpc_frequency<=0: raise ValueError("QPC frequency must be positive")
    with open(latency,newline="",encoding="utf-8") as f: trace=list(csv.DictReader(f))
    if not trace or trace[-1]["event"]!="dropped" or int(trace[-1]["time_ns"]):
        raise ValueError("Incomplete or overflowing host trace")
    times=[int(r["time_ns"]) for r in trace if r["event"] in ("original_submit","interpolated_submit") and int(r["scene"])==scene]
    if not times: raise ValueError("No requested scene")
    first,last=min(times),max(times)
    with open(presentation,newline="",encoding="utf-8-sig") as f: all_rows=list(csv.DictReader(f))
    if not all_rows or "QPCTime" not in all_rows[0]: raise ValueError("Require PresentMon v1 --qpc_time records")
    rows=[r for r in all_rows if first<=int(r["QPCTime"])*1e9/qpc_frequency<=last]
    if not rows: raise ValueError("No matching presentation timestamps")
    shown=[r for r in rows if r["Dropped"]=="0"]
    metrics={}
    for key in ("msInPresentAPI","msUntilRenderComplete","msUntilDisplayed","msBetweenDisplayChange","msGPUActive"):
        values=[]
        for r in shown:
            try: value=float(r[key])
            except (ValueError,KeyError): continue
            if math.isfinite(value): values.append(value)
        metrics[key]=stats(values)
    stamps=[int(r["QPCTime"])*1e9/qpc_frequency for r in rows]
    return {"frames":len(rows),"displayed_frames":len(shown),"dropped_frames":len(rows)-len(shown),
        "observed_span_ms":(max(stamps)-min(stamps))/1e6,
        "combat_span_ms":(last-first)/1e6,
        "span_coverage":(max(stamps)-min(stamps))/(last-first) if last>first else 0,
        "present_modes":dict(collections.Counter(r["PresentMode"] for r in rows)),"metrics":metrics,
        "scope":"Windows ETW estimates for displayed frames; dropped frames counted separately. Present-to-completion is not total GPU execution time; present-to-display is not input-to-photon latency. Check coverage before comparing."}

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("presentation");ap.add_argument("latency");ap.add_argument("--qpc-frequency",type=int,required=True)
    args=ap.parse_args();print(json.dumps(summarize(args.presentation,args.latency,args.qpc_frequency),indent=2))
