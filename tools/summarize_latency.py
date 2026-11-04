# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize host timing provenance, never physical input-to-photon latency."""
import argparse
import csv
import json

def stats(values):
    values = sorted(values)
    if not values:
        return None
    def p(q):
        return round(values[int((len(values)-1)*q)], 4)
    return {"samples": len(values), "p50_ms": p(.5), "p95_ms": p(.95), "p99_ms": p(.99), "max_ms": round(values[-1], 4)}

def summarize(path, scene=7):
    with open(path, newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    if not rows or rows[-1]["event"] != "dropped":
        raise ValueError("Trace is incomplete: clean-shutdown footer missing")
    dropped = sum(int(r["time_ns"]) for r in rows if r["event"] == "dropped")
    frames = [r for r in rows if r["event"] in ("original_submit", "interpolated_submit") and int(r["scene"]) == scene]
    shown = [r for r in frames if r["window_submitted"] == "1" and int(r["sampled_ns"]) > 0]
    newest, history, weighted, queued = [], [], [], []
    for r in shown:
        now, sample = int(r["time_ns"]), int(r["sampled_ns"])
        age = (now-sample)/1e6
        if age < 0:
            raise ValueError("non-monotonic input provenance")
        newest.append(age)
        queued.append((now-int(r["queued_ns"]))/1e6)
        # A mixed image has no single input timestamp. This describes only
        # matched geometry with a valid previous snapshot, not UI/fallbacks.
        old = int(r["previous_sampled_ns"])
        alpha = float(r["alpha"])
        if old > 0 and old <= sample and 0 <= alpha <= 1:
            delay = (1-alpha)*(sample-old)/1e6
            history.append(delay)
            weighted.append(age+delay)
    dequeue = [(int(r["time_ns"])-int(r["queued_ns"]))/1e6 for r in rows
               if r["event"] == "dequeue" and int(r["scene"]) == scene and int(r["queued_ns"]) > 0]
    gaps = [(int(b["time_ns"])-int(a["time_ns"]))/1e6 for a,b in zip(shown,shown[1:])]
    # Diagnostic source-time progression, not image/animation motion. Queue
    # publication timestamps approximate source times and include backpressure.
    source_steps, fractions = [], []
    previous_source = None
    for r in shown:
        old, current = int(r["previous_queued_ns"]), int(r["queued_ns"])
        alpha = float(r["alpha"])
        if old <= 0 or current <= old or not 0 <= alpha <= 1:
            previous_source = None
            continue
        source = old + alpha*(current-old)
        if previous_source is not None: source_steps.append((source-previous_source)/1e6)
        previous_source = source
        fractions.append(alpha)
    return {"path": str(path), "scene": scene, "submitted_frames": len(shown),
            "unavailable_swapchains": sum(r["window_submitted"] != "1" for r in frames),
            "trace_dropped_rows": dropped, "latest_sample_to_cpu_submission": stats(newest),
            "packet_queue_wait": stats(dequeue), "packet_publish_to_cpu_submission": stats(queued),
            "matched_geometry_history_component": stats(history),
            "matched_geometry_weighted_sample_age_proxy": stats(weighted),
            "cpu_submission_interval": stats(gaps),
            "matched_geometry_source_interval_proxy": stats(source_steps),
            "repeated_source_times": sum(abs(delta)<.01 for delta in source_steps),
            "latest_endpoint_fraction": sum(alpha>=.999999 for alpha in fractions)/len(fractions) if fractions else None,
            "scope": "Host timestamps and latest-sample provenance only. Not visible-response latency, GPU completion, compositor timing or display scanout. Mixed-image weighted ages apply only to matched interpolated geometry."}

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths",nargs="+")
    parser.add_argument("--scene",type=int,default=7)
    args=parser.parse_args()
    print(json.dumps([summarize(path,args.scene) for path in args.paths],indent=2))
