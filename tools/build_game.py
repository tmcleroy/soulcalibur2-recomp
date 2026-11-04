#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Lift Soulcalibur II's XBE and build the native runner.

The runtime is a dependency, not a copy: third_party/doa2u-re (pinned) holds
the shared DOA runtime. The build merges its recomp-runtime/ with this repo's
runtime/ into build/src/, where a file in runtime/ replaces the shared file of
the same name. Every replaced file is game-specific (addresses, layouts or
game hooks for XDK 5455); docs/runtime.md lists them and why.

  tools/build_game.py lift     disassemble and lift private/disc/Default.xbe
  tools/build_game.py build    merge the runtime and build the runner
  tools/build_game.py launcher build the launcher (launcher/)
  tools/build_game.py app      both, as build/Soulcalibur II Recomp.app (the launcher with the game beside it)
  tools/build_game.py all      lift and build

Generated code, the disc and every derived file stay under private/.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Release scripts build elsewhere (tools/release/linux.sh): SC2_SRC_DIR, SC2_RUNNER_DIR, SC2_LAUNCHER_DIR and extra
# CMake arguments in SC2_CMAKE_ARGS (space separated).
EXTRA_CMAKE = os.environ.get("SC2_CMAKE_ARGS", "").split()
LIFTER = ROOT / "tools/xboxrecomp"
SHARED = ROOT / "third_party/doa2u-re"
PRIVATE = ROOT / "private"
PROGRAM = PRIVATE / "program"
XBE = PRIVATE / "disc/Default.xbe"
XBE_SHA256 = "7ce12851850ab46766f77c612b251afe7c8bd81f879e11b4836bdf766be542c6"
PYTHON = PRIVATE / "venv/bin/python"
# runtime/ files with no shared counterpart, built into the runner.
GAME_SOURCES = ["sc2_scene_probe.cpp", "sc2_graphics_inventory.cpp", "sc2_hooks.c", "sc2_settings.cpp", "sc2_input.cpp", "sc2_menu.cpp", "sc2_training.cpp", "sc2_guest.cpp", "sc2_netplay.cpp", "sc2_state.cpp", "sc2_frame.cpp", "sc2_wm_select.c", "sc2_android.cpp", "sc2_touch.cpp", "x87_math/sin.c", "x87_math/cos.c", "x87_math/__sin.c", "x87_math/__cos.c", "x87_math/__rem_pio2.c", "x87_math/__rem_pio2_large.c", "x87_math/atan.c", "x87_math/atan2.c", "x87_math/log2.c", "x87_math/log2_data.c", "x87_math/expm1.c", "../recomp-common/launcher/src/online.cpp", "../recomp-common/launcher/src/run_program.cpp", "../recomp-common/menu.cpp", "../recomp-common/settings.cpp"]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def run(args, log, cwd=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    print("  " + " ".join(str(a) for a in args[:4]) + (" ..." if len(args) > 4 else ""), flush=True)
    with open(log, "w") as out:
        if subprocess.call([str(a) for a in args], cwd=cwd, stdout=out, stderr=subprocess.STDOUT):
            tail = log.read_text(errors="replace").splitlines()[-30:]
            sys.exit("failed, see %s:\n%s" % (log, "\n".join(tail)))


def program_manifest(generated):
    """The runtime CMake's manifest of the generated chunks, and their uninitialized-EBP count."""
    names = sorted(p.name for p in generated.glob("recomp_[0-9][0-9][0-9][0-9].c"))
    names += ["recomp_dispatch.c", "recomp_funcs.h"]
    text, ebp = "", 0
    for name in names:
        path = generated / name
        text += "%s\t%d\t%s\n" % (name, path.stat().st_size, sha256(path))
        if name.endswith(".c"):
            ebp += len(re.findall(r"\n[ \t]+uint32_t ebp;[ \r]*\n", path.read_text(errors="replace")))
    return hashlib.sha256(text.encode()).hexdigest(), ebp


def bound_addresses():
    """Every guest address a runtime/ lookup table binds.

    The lifter gets them as --manual-functions: their converted bodies are
    left out and every call or tail jump to them goes through the runtime's
    lookup, so a tail jump into a replaced SDK function cannot run the
    original (docs/bring-up.md)."""
    found = set()
    for path in sorted((ROOT / "runtime").glob("*.c")):
        text = path.read_text()
        # Hooks wrap the converted function and call it, so it stays converted.
        for body in re.findall(r"RecompFunction recomp_(?!sc2_hooks_)\w*lookup_manual\(uint32_t \w+\)\n\{(.*?)\n\}",
                               text, re.S):
            found.update(int(m, 16) for m in re.findall(r"0x([0-9a-fA-F]{5,8})u", body))
        # Named enum constants (input adapter) referenced by those tables.
        for name, value in re.findall(r"(\w+_ADDRESS) = 0x([0-9a-fA-F]+)u", text):
            found.add(int(value, 16))
    return sorted(found)


def manual_list():
    path = PROGRAM / "manual_functions.json"
    entries = ["0x%08X" % a for a in bound_addresses()]
    path.write_text(json.dumps(entries, indent=0) + "\n")
    return path, hashlib.sha256(path.read_bytes()).hexdigest()


def lift():
    if sha256(XBE) != XBE_SHA256:
        sys.exit("private/disc/Default.xbe is not the supported Soulcalibur II (USA) executable")
    logs = PRIVATE / "logs"
    seeds = ROOT / "tools/sc2/seed_functions.json"
    run([PYTHON, "-u", "-m", "tools.xbe_parser", XBE, "--json", PROGRAM / "analysis.json"],
        logs / "xbe-parser.log", LIFTER)
    run([PYTHON, "-u", "-m", "tools.disasm", XBE, "-o", PROGRAM / "disasm",
         "--analysis-json", PROGRAM / "analysis.json", "--seed-functions", seeds],
        logs / "disasm.log", LIFTER)
    rows = json.loads((PROGRAM / "disasm/functions.json").read_text())
    rows = [{k: v for k, v in r.items() if k not in ("calls_to", "called_by")} for r in rows]
    (PROGRAM / "functions.json").write_text(json.dumps(rows))
    none = PROGRAM / "none"
    manual, manual_sha256 = manual_list()
    run([PYTHON, "-u", "-m", "tools.recomp", XBE, "--all", "--split", "1000",
         "--manual-functions", manual,
         "--game-name", "Soulcalibur II", "--disasm-dir", none,
         "--functions", PROGRAM / "functions.json", "--func-id-dir", none, "--abi-dir", none,
         "--icall-sites", PROGRAM / "none.json", "--gen-dir", PROGRAM / "generated",
         "--output-dir", PROGRAM / "metadata"], logs / "lift.log", LIFTER)
    manifest, ebp = program_manifest(PROGRAM / "generated")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=LIFTER, text=True).strip()
    (PROGRAM / "receipt.json").write_text(json.dumps(dict(
        xbe_sha256=XBE_SHA256, lifter_commit=revision, manifest_sha256=manifest, ebp_count=ebp,
        manual_sha256=manual_sha256), indent=2) + "\n")
    print("lifted: manifest %s, uninitialized EBP prologues %d" % (manifest[:12], ebp))


def merge():
    """build/src/recomp-runtime: shared files linked, runtime/ files replacing them."""
    src = ROOT / os.environ.get("SC2_SRC_DIR", "build/src")
    if src.exists():
        shutil.rmtree(src)
    runtime = src / "recomp-runtime"
    runtime.mkdir(parents=True)
    own = {p.name: p for p in (ROOT / "runtime").iterdir() if p.is_file()}
    for path in sorted((SHARED / "recomp-runtime").iterdir()):
        if path.is_file():
            os.symlink(own.pop(path.name, path), runtime / path.name)
    for name, path in own.items():
        os.symlink(path, runtime / name)
    # (the x87's math, musl's: the same on every host)
    os.symlink(ROOT / "runtime/x87_math", runtime / "x87_math")
    # (the version, which online play says to the other side: two copies must have the same)
    (runtime / "sc2_version.h").write_text('#define SC2_VERSION "%s"\n' % (ROOT / "VERSION").read_text().strip())
    os.symlink(SHARED / "xbe", src / "xbe")
    os.symlink(ROOT / "recomp-common", src / "recomp-common")
    os.symlink(SHARED / "tools", src / "tools")
    os.symlink(PRIVATE, src / "private")
    return runtime


def build():
    receipt = json.loads((PROGRAM / "receipt.json").read_text())
    if program_manifest(PROGRAM / "generated") != (receipt["manifest_sha256"], receipt["ebp_count"]):
        sys.exit("private/program/generated differs from its receipt; lift again")
    if manual_list()[1] != receipt.get("manual_sha256"):
        sys.exit("runtime/ binds a different set of functions than the last lift; lift again")
    runtime = merge()
    out = ROOT / os.environ.get("SC2_RUNNER_DIR", "build/runner")
    logs = PRIVATE / "logs"
    run(["cmake", "-S", runtime, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", *EXTRA_CMAKE,
         "-DRECOMP_PROGRAM_DIR=%s" % (PROGRAM / "generated"),
         "-DRECOMP_XBE_SHA256=%s" % XBE_SHA256,
         "-DRECOMP_PROGRAM_MANIFEST_SHA256=%s" % receipt["manifest_sha256"],
         "-DRECOMP_PROGRAM_EBP_EXPECTED=%d" % receipt["ebp_count"],
         "-DRECOMP_GAME_SOURCES=%s" % ";".join(GAME_SOURCES),
         "-DRECOMP_PROGRAM_INCLUDE=%s" % (runtime / "x87_math.h"),
         "-DCMAKE_PROJECT_INCLUDE=%s" % (ROOT / "tools/file_prefix.cmake"), "-DSC2_SOURCE_ROOT=%s" % ROOT],
        logs / "configure.log")
    jobs = str(min(6, os.cpu_count() or 1))
    run(["cmake", "--build", out, "--parallel", jobs, "--target", "recomp_program_runner"],
        logs / "build.log")
    print("built %s" % (out / "recomp_program_runner"))


def launcher():
    out = ROOT / os.environ.get("SC2_LAUNCHER_DIR", "build/launcher")
    logs = PRIVATE / "logs"
    run(["cmake", "-S", ROOT / "launcher", "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", *EXTRA_CMAKE],
        logs / "launcher-configure.log")
    run(["cmake", "--build", out, "--parallel", str(min(6, os.cpu_count() or 1))], logs / "launcher-build.log")
    print("built %s" % (out / "soulcalibur2-launcher"))


APP_NAME = "Soulcalibur II Recomp"
APP_ID = "io.github.tmcleroy.soulcalibur2"


def app():
    """build/<APP_NAME>.app: the launcher, the game beside it as soulcalibur2, signed ad hoc. It links the
    system's SDL3 and shader libraries (Homebrew): a build for this Mac, not a release."""
    bundle = ROOT / "build" / (APP_NAME + ".app")
    if bundle.exists():
        shutil.rmtree(bundle)
    macos = bundle / "Contents/MacOS"
    macos.mkdir(parents=True)
    (bundle / "Contents/Resources").mkdir()
    shutil.copy2(ROOT / "build/launcher/soulcalibur2-launcher", macos / "soulcalibur2-launcher")
    shutil.copy2(ROOT / "build/runner/recomp_program_runner", macos / "soulcalibur2")
    # (the Shader feature's presets, sc2_settings.h ShaderPreset: tools/shaders.sh)
    shaders = subprocess.check_output([str(ROOT / "tools/shaders.sh")], text=True).strip()
    shutil.copytree(shaders, bundle / "Contents/Resources/shaders")
    version = (ROOT / "VERSION").read_text().strip()
    plist = (ROOT / "tools/release/Info.plist").read_text()
    for key, value in (("@VERSION@", version), ("@NAME@", APP_NAME), ("@APP_ID@", APP_ID),
                       ("@EXE@", "soulcalibur2-launcher")):
        plist = plist.replace(key, value)
    (bundle / "Contents/Info.plist").write_text(plist)
    subprocess.check_call(["codesign", "--force", "--deep", "-s", "-", str(bundle)])
    print("built %s" % bundle)


if __name__ == "__main__":
    step = sys.argv[1] if len(sys.argv) > 1 else "all"
    if step in ("lift", "all"):
        lift()
    if step in ("build", "all", "app"):
        build()
    if step in ("launcher", "app"):
        launcher()
    if step == "app":
        app()
