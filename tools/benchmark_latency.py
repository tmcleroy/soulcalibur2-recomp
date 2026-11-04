# SPDX-License-Identifier: GPL-3.0-or-later
"""Sequential Windows off/on/on/off host-timing comparison with private saves."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from summarize_latency import summarize
from summarize_gpu_timing import summarize as summarize_gpu
from summarize_presentation import summarize as summarize_present

def ensure_idle():
    check=subprocess.run(["powershell", "-NoProfile", "-Command",
        "if (Get-Process -Name recomp_program_runner,soulcalibur2,soulcalibur2-launcher,recomp_graphics_inventory_test,sc2_launcher -ErrorAction SilentlyContinue) { exit 1 }"],capture_output=True)
    if check.returncode:
        raise RuntimeError("Close the existing game/launcher before starting a latency test")

def window_state(pid):
    user32=ctypes.windll.user32
    user32.IsIconic.argtypes=[wintypes.HWND]
    user32.IsWindowVisible.argtypes=[wintypes.HWND]
    user32.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
    windows=[]
    @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    def visitor(hwnd,param):
        owner=ctypes.c_ulong();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==pid:
            rect=wintypes.RECT();user32.GetClientRect(hwnd,ctypes.byref(rect))
            windows.append(dict(visible=bool(user32.IsWindowVisible(hwnd)),
                minimized=bool(user32.IsIconic(hwnd)),width=rect.right-rect.left,height=rect.bottom-rect.top))
        return True
    user32.EnumWindows(visitor,0)
    return windows

def close(process):
    user32=ctypes.windll.user32
    @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    def visitor(hwnd,param):
        owner=ctypes.c_ulong();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==process.pid:user32.PostMessageW(hwnd,0x0010,0,0)
        return True
    user32.EnumWindows(visitor,0)
    try:return process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate();process.wait()
        raise RuntimeError("Game did not close cleanly; trace is invalid")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe",type=Path,required=True)
    ap.add_argument("--disc",type=Path,required=True)
    ap.add_argument("--seed-storage",type=Path,required=True,help="Read-only source containing partition1")
    ap.add_argument("--output",type=Path,required=True,help="New directory under private/")
    ap.add_argument("--rate",type=float,default=144)
    ap.add_argument("--frames-in-flight",type=int,choices=(1,2,3),help="Diagnostic queue limit; default leaves SDL unchanged")
    ap.add_argument("--gpu-timing",action="store_true",help="Record asynchronous completion bounds and CPU acquire/submit timing")
    ap.add_argument("--presentmon",type=Path,help="Optional PresentMon console executable for external GPU/display timing")
    ap.add_argument("--sequence",nargs="+",help="Explicit render rates, e.g. 144 144; default off/rate/rate/off")
    ap.add_argument("--seconds",type=float,default=75)
    ap.add_argument("--hidden",action="store_true",help="Diagnostic only; a hidden window may provide no swapchain samples")
    args=ap.parse_args()
    if os.name!="nt":ap.error("This process/window harness currently supports Windows only")
    if not 30<=args.rate<=1000 or args.seconds<60:ap.error("Rate must be 30..1000; allow at least 60 seconds per run")
    sequence=args.sequence or ["off",str(args.rate),str(args.rate),"off"]
    if any(x != "off" and not 30 <= float(x) <= 1000 for x in sequence): ap.error("Invalid sequence rate")
    args.output=args.output.resolve()
    if "private" not in args.output.parts:ap.error("Keep generated validation output under private/")
    args.output.mkdir(parents=True,exist_ok=False)
    results=[]
    for index,rate in enumerate(sequence,1):
        ensure_idle()
        folder=args.output/f"{index}-{rate}";folder.mkdir()
        shutil.copytree(args.seed_storage/"partition1",folder/"storage/partition1")
        settings=folder/"settings.ini"
        settings.write_text("[features]\npreset=custom\ngraphics_prepare=1\n",encoding="utf-8")
        trace=folder/"latency.csv"
        env=os.environ.copy()
        for key in ["SC2_RAM_DUMP","SC2_RAM_DUMP_AT","RECOMP_D3D_FRAME_DUMP","RECOMP_RENDER_TRACE"]:env.pop(key,None)
        env.update(RECOMP_LATENCY_TRACE=str(trace),RECOMP_RENDER_FPS=rate)
        env.pop("RECOMP_GPU_FRAMES_IN_FLIGHT",None)
        if args.frames_in_flight and rate != "off": env["RECOMP_GPU_FRAMES_IN_FLIGHT"]=str(args.frames_in_flight)
        env.pop("RECOMP_GPU_TIMING",None)
        if args.gpu_timing: env["RECOMP_GPU_TIMING"]=str(folder/"gpu-timing.csv")
        if args.hidden:env["RECOMP_HEADLESS"]="1"
        else:env.pop("RECOMP_HEADLESS",None)
        cmd=[str(args.exe.resolve()),"--disc",str(args.disc.resolve()),"--storage",str(folder/"storage"),
             "--settings",str(settings),"--skip-intro","--input-script-only"]
        for step in ["5:120:start","34#2:120:A","34#2:300:A","34#2:600:A","6:150:A",
                     "6:200:down","6:220:down","6:240:down","6:260:down","6:320:A",
                     "6#2:150:A","6#2:300:A","10:150:A","10:300:A","10:700:down","10:900:A"]:
            cmd += ["--input-scene",step]
        for poll in range(600,1800,40):cmd += ["--input-scene",f"7:{poll}:X"]
        print(f"Starting {index}/{len(sequence)}: interpolation {rate}",flush=True)
        with (folder/"stdout.log").open("w") as out,(folder/"stderr.log").open("w") as err:
            process=subprocess.Popen(cmd,env=env,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
            monitor=None
            monitor_log=None
            try:
                if args.presentmon:
                    monitor_log=(folder/"presentmon.log").open("w")
                    monitor=subprocess.Popen([str(args.presentmon.resolve()),"--process_name",args.exe.name,
                        "--session_name",f"SC2Timing{process.pid}","--output_file",str(folder/"presentmon.csv"),
                        "--v1_metrics","--qpc_time","--no_console_stats","--no_track_input",
                        "--timed",str(int(args.seconds)+3),"--terminate_after_timed"],
                        stdout=monitor_log,stderr=monitor_log,creationflags=subprocess.CREATE_NO_WINDOW)
                minimized_seen=False
                deadline=time.monotonic()+args.seconds
                with (folder/"window-state.jsonl").open("w",encoding="utf-8") as windows:
                    while process.poll() is None and time.monotonic()<deadline:
                        state=window_state(process.pid)
                        minimized_seen |= any(w["minimized"] for w in state)
                        windows.write(json.dumps(dict(remaining_seconds=deadline-time.monotonic(),windows=state))+"\n")
                        windows.flush()
                        time.sleep(1)
                code=close(process) if process.poll() is None else process.returncode
            finally:
                if process.poll() is None:process.terminate();process.wait()
                if monitor:
                    try: monitor.wait(timeout=10)
                    except subprocess.TimeoutExpired: monitor.terminate();monitor.wait()
                if monitor_log: monitor_log.close()
            if monitor and monitor.returncode != 0: raise RuntimeError("PresentMon capture failed; inspect its log")
        if minimized_seen and not args.hidden:
            raise RuntimeError("Window was minimized; comparison invalid, rerun unminimized")
        if args.presentmon and not (folder/"presentmon.csv").exists():
            raise RuntimeError("PresentMon returned no frame data; GPU/display measurement is unavailable")
        if code!=0:raise RuntimeError(f"Run {index} failed with exit {code}")
        if args.frames_in_flight and rate != "off" and f"GPU diagnostic: allowed frames in flight={args.frames_in_flight}" not in (folder/"stderr.log").read_text(encoding="utf-8",errors="replace"):
            raise RuntimeError("Executable did not confirm requested GPU queue limit")
        report=summarize(trace)
        if report["submitted_frames"]<300 or report["trace_dropped_rows"]:
            raise RuntimeError("Insufficient combat samples or trace overflow; do not report latency")
        if args.gpu_timing:
            report["gpu_timing"]=summarize_gpu(folder/"gpu-timing.csv")
            if report["gpu_timing"]["fence_failures"] or report["gpu_timing"]["incomplete_fences"]:
                raise RuntimeError("Incomplete GPU fence measurement")
        if args.presentmon:
            frequency=ctypes.c_longlong()
            if not ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(frequency)):
                raise RuntimeError("QPC frequency unavailable")
            report["presentation"]=summarize_present(folder/"presentmon.csv",trace,frequency.value)
            report["qpc_frequency"]=frequency.value
            if report["presentation"]["span_coverage"] < .95:
                raise RuntimeError("PresentMon did not cover the full combat interval")
        report.update(interpolation=rate,exit_code=code,visible=not args.hidden,
                      frames_in_flight=(args.frames_in_flight or "SDL default") if rate != "off" else "SDL default")
        results.append(report)
        (args.output/"summary.json").write_text(json.dumps(results,indent=2),encoding="utf-8")
        print(json.dumps(report),flush=True)
    print("Finished all runs; all game processes have exited.",flush=True)

if __name__=="__main__":main()
