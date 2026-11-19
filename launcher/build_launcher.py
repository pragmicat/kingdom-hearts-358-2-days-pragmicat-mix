"""Compile the GUI launcher with installed Visual Studio tools; never run it."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def write_if_changed(path: Path, content: str) -> None:
    if not path.is_file() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8")


def build_key(dependencies, inputs, command, environment, tools):
    paths = set(inputs + tools)
    for dependency in dependencies:
        data = json.loads(dependency.read_text(encoding="utf-8-sig"))["Data"]
        if data.get("ImportedModules") or data.get("ImportedHeaderUnits"):
            raise ValueError("Module dependencies need a fresh compilation")
        paths.add(Path(data["Source"]))
        paths.update(Path(name) for name in data["Includes"])
    digest = hashlib.sha256(json.dumps({"command": command, "environment": {
        name: environment.get(name, "") for name in ("INCLUDE", "LIB", "LIBPATH", "PATH", "CL", "_CL_", "LINK", "_LINK_")
    }}, sort_keys=True).encode())
    for path in sorted(paths, key=lambda p: str(p.resolve()).casefold()):
        digest.update(str(path.resolve()).encode("utf-8"))
        digest.update(b"\0" + hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def unchanged(cache: Path, output: Path, key: str) -> bool:
    try:
        record = json.loads(cache.read_text(encoding="utf-8"))
        return record["input_sha256"] == key and record["output_sha256"] == hashlib.sha256(output.read_bytes()).hexdigest()
    except (OSError, ValueError, KeyError, TypeError):
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=HERE / "build" / "Kingdom Hearts 358-2 Days - Pragmicat Mix.exe")
    parser.add_argument("--rebuild", action="store_true", help="Recompile even when the launcher and its inputs are unchanged")
    args = parser.parse_args()
    where = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = subprocess.check_output(
        [str(where), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"], text=True
    ).strip()
    if not installation:
        raise SystemExit("Visual Studio C++ build tools are required.")
    visual_studio = Path(installation)
    vcvars = visual_studio / "VC/Auxiliary/Build/vcvars64.bat"
    # The batch path comes only from Microsoft's installed-tool discovery.
    settings = subprocess.check_output(f'"{vcvars}" >nul && set', shell=True, text=True)
    environment = {name.upper(): value for name, value in os.environ.items()}
    environment.update((name.upper(), value) for name, value in
                       (line.split("=", 1) for line in settings.splitlines() if "=" in line))
    environment["VSLANG"] = "1033"
    compilers = sorted((visual_studio / "VC/Tools/MSVC").glob("*/bin/Hostx64/x64/cl.exe"))
    if not compilers:
        raise SystemExit("No x64 C++ compiler found.")
    output = args.out.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("VERSION must contain a major.minor.patch version.")
    if not (ROOT / "assets/kingdom-hearts-358-2-days-pragmicat-mix-icon.ico").is_file():
        raise SystemExit("The Kingdom Hearts 358/2 Days icon is missing.")
    resource_compiler = shutil.which("rc.exe", path=environment.get("PATH"))
    if not resource_compiler:
        raise SystemExit("The Windows SDK resource compiler was not found. Install a Windows SDK with Visual Studio.")
    write_if_changed(output.parent / "version.h",
        "#define PRAGMICAT_VERSION_NUM " + version.replace(".", ",") + ",0\n"
        '#define PRAGMICAT_VERSION_STRING "' + version + '"\n')
    resources = output.parent / "pragmicat_mix_launcher.res"
    payloads = [
        (200, "mods/Pragmicat Mix/native/windows-x64/pragmicat_mix.dll", ROOT / "mod/native/windows-x64/pragmicat_mix.dll", False),
        (201, "mods/Pragmicat Mix/mod.ini", ROOT / "mod/mod.ini", False),
        (202, "mods/Pragmicat Mix/parameters.ini", ROOT / "mod/parameters.ini", False),
        (203, "mods/Pragmicat Practice/mod.ini", ROOT / "practice/mod.ini", True),
        (204, "mods/Pragmicat Practice/scripts/main.lua", ROOT / "practice/scripts/main.lua", True),
        (205, "mods/Pragmicat Practice/missions/missions.txt", ROOT / "practice/missions/missions.txt", True),
        (206, "licenses/project.txt", ROOT / "LICENSE", False),
        (207, "licenses/native-sdk.txt", ROOT / "vendor/sdk/LICENSE", False),
        (208, "licenses/artwork.txt", ROOT / "assets/NOTICE.txt", False),
        (209, "assets/kingdom-hearts-358-2-days-pragmicat-mix-logo.png", ROOT / "assets/kingdom-hearts-358-2-days-pragmicat-mix-logo.png", False),
    ]
    for _, _, source, _ in payloads:
        if not source.is_file():
            raise SystemExit(f"Required launcher payload is missing: {source.relative_to(ROOT)}. Build the combat mod first.")
    header = ['#pragma once', 'struct PayloadEntry { unsigned id; const wchar_t* relative; bool practice_only; };',
              'inline constexpr PayloadEntry kPayloadEntries[] = {']
    header += [f'    {{{number}, L"{relative}", {str(practice).lower()}}},' for number, relative, _, practice in payloads]
    write_if_changed(output.parent / "payload_entries.h", "\n".join(header + ['};', '']))
    payload_rc = output.parent / "payload.rc"
    write_if_changed(payload_rc, '#include <windows.h>\n' + '\n'.join(
        f'{number} RCDATA "{source.resolve().as_posix()}"' for number, _, source, _ in payloads) + '\n')
    payload_res = output.parent / "payload.res"
    dependency_dir = output.parent / "launcher-dependencies"
    dependency_dir.mkdir(exist_ok=True)
    command = [str(compilers[-1]), "/nologo", "/std:c++17", "/EHsc", "/utf-8", "/W4", "/WX", "/O2", "/MT",
               "/DUNICODE", "/D_UNICODE", "/D_WIN32_WINNT=0x0A00", "/DWINVER=0x0A00", "/guard:cf",
               "/sourceDependencies", str(dependency_dir) + os.sep,
               "/I" + str(output.parent), "/Fo" + str(output.parent) + os.sep, "/Fe" + str(output),
               str(HERE / "launcher.cpp"), str(HERE / "setup.cpp"), str(resources), str(payload_res), "/link",
               "/SUBSYSTEM:WINDOWS", "/MANIFEST:NO", "/DYNAMICBASE", "/NXCOMPAT", "/HIGHENTROPYVA", "/guard:cf",
               "user32.lib", "gdi32.lib", "gdiplus.lib", "ole32.lib", "shell32.lib", "uuid.lib", "bcrypt.lib", "comctl32.lib"]
    inputs = [Path(__file__), HERE / "launcher.cpp", HERE / "setup.cpp", HERE / "setup.h",
              HERE / "resources.rc", HERE / "launcher.manifest", ROOT / "VERSION",
              ROOT / "assets/kingdom-hearts-358-2-days-pragmicat-mix-icon.ico"]
    inputs += [source for _, _, source, _ in payloads]
    dependencies = [dependency_dir / "launcher.cpp.json", dependency_dir / "setup.cpp.json"]
    tools = [compilers[-1], compilers[-1].parent / "link.exe", Path(resource_compiler)]
    cache = HERE / "build/launcher-cache.json"
    cache.parent.mkdir(exist_ok=True)
    try:
        key = build_key(dependencies, inputs, command, environment, tools)
    except (OSError, ValueError, KeyError, TypeError):
        key = None
    if not args.rebuild and key and unchanged(cache, output, key):
        print(f"Unchanged: {output}")
    else:
        subprocess.run([resource_compiler, "/nologo", "/I", str(output.parent), "/I", str(ROOT / "assets"),
                        "/fo", str(resources), str(HERE / "resources.rc")], cwd=HERE, env=environment, check=True)
        subprocess.run([resource_compiler, "/nologo", "/fo", str(payload_res), str(payload_rc)], env=environment, check=True)
        subprocess.run(command, cwd=output.parent, env=environment, check=True)
        key = build_key(dependencies, inputs, command, environment, tools)
        cache.write_text(json.dumps({"input_sha256": key, "output_sha256": hashlib.sha256(output.read_bytes()).hexdigest()}), encoding="utf-8")
        print(f"Built: {output}")
    print(f"SHA256: {hashlib.sha256(output.read_bytes()).hexdigest()}")
    print("Launcher was not run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
