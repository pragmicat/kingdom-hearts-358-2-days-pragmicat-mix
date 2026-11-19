"""Optional real-game regression, using the native port's headless input scripts.

Supply your engine, prepared data (containing extracted/), and built combat mod.
Every case retains a fresh temporary work profile; no existing saves are read.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
ATTACK = re.compile(r"(light|technique) attack (\d+), native graph (\d+).*?\b(ground|air)\b")
CRASH = re.compile(r"first-chance exception|unhandled exception|exception 0x|native function:.*stopped|KHDAYS_INPUT: cannot read", re.I)
SETTINGS = """version = 1
preset = kh2
log = on
[mods]
native_code = on
[save]
autosave = off
[video]
display = window
internal_resolution = 1
screens = one
[network]
online = off
check_updates = off
"""


def cases():
    result = {}
    def pulses(key, begin, end, interval=12):
        return [(at, "+" + key) for at in range(begin, end, interval)] + [
            (at + 4, "-" + key) for at in range(begin, end, interval)]
    result["ground-light"] = pulses("A", 180, 850)
    result["tech-neutral"] = pulses("X", 180, 850, 24)
    for name, x, y in [("up", 0, 32767), ("down", 0, -32767),
                        ("left", -32767, 0), ("right", 32767, 0)]:
        steps = []
        for at in range(180, 850, 36):
            # A real host stick supplies both a quantized DS direction and
            # analog refinement. The official script exposes them separately.
            steps += [(at - 2, f"~{x},{y}"), (at - 2, "+" + name.upper()),
                      (at, "+X"), (at + 4, "-X"),
                      (at + 6, "-" + name.upper()), (at + 6, "~0,0")]
        result["tech-" + name] = steps
    result["air-light"] = []
    for at in range(180, 800, 180):
        result["air-light"] += [(at, "+B"), (at + 8, "-B")] + pulses("A", at + 12, at + 80)
    result["drive-cycle"] = [(160, "+SELECT"), (166, "-SELECT"),
                              (550, "+SELECT"), (556, "-SELECT")]
    result["drive-cycle"] += pulses("A", 220, 490) + pulses("A", 650, 1030)
    result["signature-chord"] = []
    for at in (180, 400, 640):
        result["signature-chord"] += [(at, "+A"), (at, "+X"),
                                      (at + 10, "-A"), (at + 10, "-X")]
    result["signature-chord"] += pulses("A", 820, 1050)
    result["signature-directions"] = []
    directions = [("UP", 0, 32767), ("DOWN", 0, -32767),
                  ("LEFT", -32767, 0), ("RIGHT", 32767, 0), ("DOWN", 0, -32767)]
    for at, (key, x, y) in zip((180, 380, 580, 780, 980), directions):
        result["signature-directions"] += [
            (at - 2, "+" + key), (at - 2, f"~{x},{y}"),
            (at, "+A"), (at, "+X"), (at + 10, "-A"), (at + 10, "-X"),
            (at + 12, "-" + key), (at + 12, "~0,0")]
    result["signature-directions"] += pulses("A", 1140, 1321)
    return result


def mod_id(folder):
    text = (folder / "mod.ini").read_text(encoding="utf-8-sig")
    match = re.search(r"^\s*id\s*=\s*([^\r\n]+)", text, re.M)
    return match[1].strip() if match else ""


def source_mods(source):
    folders = [source] if (source / "mod.ini").is_file() else [
        p for p in source.iterdir() if p.is_dir() and (p / "mod.ini").is_file()]
    selected = {}
    for folder in folders:
        identity = mod_id(folder)
        if identity in {"local.pragmicat-mix", "local.pragmicat-practice"}:
            if identity in selected:
                raise ValueError("Duplicate Days mod IDs in --mods.")
            selected[identity] = folder.resolve(strict=True)
    if "local.pragmicat-mix" not in selected:
        raise ValueError("--mods must contain the built local.pragmicat-mix combat mod.")
    if not (selected["local.pragmicat-mix"] / "native/windows-x64/pragmicat_mix.dll").is_file():
        raise ValueError("Build the combat DLL before running this regression.")
    selected.setdefault("local.pragmicat-practice", ROOT / "practice")
    for folder in selected.values():
        if not (folder / "mod.ini").is_file():
            raise ValueError("The practice mod is missing.")
        for path in folder.rglob("*"):
            if path.is_symlink() or path.is_junction() or not path.resolve().is_relative_to(folder):
                raise ValueError("Mod input contains a link; supply plain built mod folders.")
    return selected


def junction(link, target):
    # One native PowerShell operation; never delete, replace, or move a link.
    def quote(path):
        return "'" + str(path).replace("'", "''") + "'"
    command = "New-Item -ItemType Junction -Path " + quote(link) + " -Target " + quote(target) + " | Out-Null"
    subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                   check=True, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if link.resolve(strict=True) != target:
        raise ValueError("The test data junction does not resolve to the supplied --data directory.")


def analyze(name, native, mods, step_count, alive, code):
    attacks = [(kind, int(identity), int(graph), group) for kind, identity, graph, group in ATTACK.findall(mods)]
    traces = [int(n) for n in re.findall(r"input: frame (\d+), game frame \d+, \d+ pad reads:", native)]
    field_frames = [int(n) for n in re.findall(r"khdays-native: frame (\d+), scene 2\b", native)]
    checks = {"alive_until_controlled_stop": alive, "no_natural_exit": code is None,
              "no_exception": not CRASH.search(native + "\n" + mods),
              "complete_weapon_animation_cache": "all 16 authored weapon animations available" in mods,
              "no_weapon_animation_fallback": not re.search(r"supplement load refused|refused missing weapon.animation tag|missing tags use safe stock", mods, re.I),
              "script_completed": len(traces) >= step_count,
              "field_progress_after_script": bool(traces and field_frames and max(field_frames) >= traces[-1] + 120)}
    def ids(text, kind, group):
        return [int(i) for k, i, _, g in ATTACK.findall(text) if k == kind and g == group]
    if name == "ground-light":
        actual = ids(mods, "light", "ground")
        checks["ground_chain_0_then_1060"] = actual[:2] == [0, 1060]
        checks["four_ground_routes"] = {0, 1060, 5070, 1}.issubset(actual)
    elif name == "tech-neutral":
        checks["neutral_technique_14021"] = 14021 in ids(mods, "technique", "ground")
    elif name.startswith("tech-"):
        # Sticks are world/camera inputs; the authored role is relative to the
        # live target. Check a directional route without assuming its heading.
        checks["directional_technique"] = bool({1, 13080, 10120, 6061, 5130}.intersection(ids(mods, "technique", "ground")))
    elif name == "air-light":
        checks["three_air_routes"] = {3, 11, 12}.issubset(ids(mods, "light", "air"))
    elif name == "drive-cycle":
        entered = "Drive: native dual-wield Roxas entered;"
        restored = "Drive: normal Story Roxas restored;"
        before, separator, active = mods.partition(entered)
        during, returned, after = active.partition(restored)
        checks["drive_entered_and_restored"] = bool(separator and returned)
        checks["dual_wield_attacks"] = {1001, 1010}.issubset(ids(during, "light", "ground"))
        checks["normal_attacks_after_restore"] = ids(after, "light", "ground")[:2] == [0, 1060]
    elif name == "signature-chord":
        _, finisher, after = mods.rpartition("ordinary Event Horizon flurry finisher.")
        checks["ordinary_flurry_finisher"] = bool(finisher)
        checks["normal_attacks_after_finisher"] = ids(after, "light", "ground")[:2] == [0, 1060]
    elif name == "signature-directions":
        finishers = list(re.finditer(r"ordinary Event Horizon (rush|flurry|light-burst) finisher\.", mods))
        checks["ordinary_rush_finisher"] = any(m[1] == "rush" for m in finishers)
        checks["ordinary_burst_finisher"] = any(m[1] == "light-burst" for m in finishers)
        after = mods[finishers[-1].end():] if finishers else ""
        checks["normal_attacks_after_finishers"] = ids(after, "light", "ground")[:2] == [0, 1060]
    return {"ok": all(checks.values()), "checks": checks, "attack_count": len(attacks),
            "attack_ids": sorted({a[1] for a in attacks}), "graphs": sorted({a[2] for a in attacks}),
            "signature_finishers": sorted(set(re.findall(r"ordinary Event Horizon (rush|flurry|light-burst) finisher\.", mods))),
            "script_steps_seen": len(traces), "script_steps_expected": step_count,
            "last_input_frame": traces[-1] if traces else None,
            "last_field_frame": max(field_frames) if field_frames else None,
            "natural_exit_code": code, "controlled_stop": alive}


def run_case(name, steps, args, output, mods):
    run = output / name
    run.mkdir()
    for identity, source in mods.items():
        shutil.copytree(source, run / "mods" / identity)
    saves = run / "saves"
    saves.mkdir()
    settings = saves / "settings.ini"
    settings.write_text(SETTINGS, encoding="utf-8")
    junction(run / "data", args.data)
    end = 1440 if name == "signature-directions" else 1140 if name in {"drive-cycle", "signature-chord"} else 900
    steps = sorted(steps) + [(end, "^0,0")]
    script = "s2 " + " ".join(str(frame) + action for frame, action in steps)
    (run / "input.txt").write_text(script + "\n", encoding="ascii")
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("KHDAYS_", "PRAGMICAT_"))}
    env.update(KHDAYS_HEADLESS="1", KHDAYS_INPUT=script, KHDAYS_TRACE_INPUT="1",
               KHDAYS_SETTINGS=str(settings), KHDAYS_PLAYER_SLOT="1", KHDAYS_UPDATES="off",
               KHDAYS_LOG="1", KHDAYS_SOUND="0", PRAGMICAT_PRACTICE="1")
    started = time.monotonic()
    alive, code = False, None
    with (run / "native.log").open("wb") as log:
        process = subprocess.Popen([str(args.engine)], cwd=run, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = process.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            alive = process.poll() is None
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
    native = (run / "native.log").read_text(encoding="utf-8", errors="replace")
    mod_log = saves / "logs/mods.log"
    text = mod_log.read_text(encoding="utf-8", errors="replace") if mod_log.is_file() else ""
    report = {"case": name, **analyze(name, native, text, len(steps), alive, code),
              "combat_dll_sha256": hashlib.sha256((run / "mods/local.pragmicat-mix/native/windows-x64/pragmicat_mix.dll").read_bytes()).hexdigest(),
              "elapsed_seconds": round(time.monotonic() - started, 2),
              "fresh_profile": str(run), "save_files_created": sorted(p.name for p in saves.glob("*.sav"))}
    (run / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    available = cases()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="Native Days game EXE")
    parser.add_argument("--data", type=Path, required=True, help="Prepared data directory containing extracted/")
    parser.add_argument("--mods", type=Path, required=True, help="Built combat mod, or directory containing the Days mods")
    parser.add_argument("--seconds", type=float, default=45, help="Per-case timeout; completion and later field progress are still required")
    parser.add_argument("--out", type=Path, default=ROOT / "tests/.build/native-combat/work", help="Parent for a new retained work profile; default is the ignored tests/.build folder")
    parser.add_argument("--case", action="append", choices=available, help="Repeat to select cases; default runs all")
    args = parser.parse_args()
    try:
        if os.name != "nt":
            raise ValueError("This regression runner requires Windows.")
        args.engine, args.data, args.mods = (p.resolve(strict=True) for p in (args.engine, args.data, args.mods))
        if not args.engine.is_file() or args.engine.suffix.lower() != ".exe":
            raise ValueError("--engine must name a native game EXE.")
        if not (args.data / "extracted").is_dir() or not any((args.data / "extracted").glob("*/manifest.json")):
            raise ValueError("--data must contain prepared extracted/<game>/manifest.json.")
        if not 10 <= args.seconds <= 600:
            raise ValueError("--seconds must be between 10 and 600.")
        mods = source_mods(args.mods)
        target = args.out.resolve()
        if any(target.is_relative_to(p) for p in (args.engine.parent, args.data, args.mods)):
            raise ValueError("--out must be outside the supplied engine, data, and mod directories.")
        target.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="native-combat-", dir=target))
        reports = []
        for name in dict.fromkeys(args.case or available):
            result = run_case(name, available[name], args, output, mods)
            reports.append(result)
            print(json.dumps({"case": name, "ok": result["ok"], "attack_ids": result["attack_ids"],
                              "failed_checks": [k for k, value in result["checks"].items() if not value]}), flush=True)
        tested_hashes = {r["combat_dll_sha256"] for r in reports}
        summary = {"ok": all(r["ok"] for r in reports) and len(tested_hashes) == 1, "engine_sha256": hashlib.sha256(args.engine.read_bytes()).hexdigest(),
                   "combat_dll_sha256": next(iter(tested_hashes)) if len(tested_hashes) == 1 else None,
                   "output": str(output), "cases": reports}
        (output / "report.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"ok": summary["ok"], "cases": len(reports), "report": str(output / "report.json")}))
        return 0 if summary["ok"] else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
