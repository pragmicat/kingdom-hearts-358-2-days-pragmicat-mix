"""Build the native mod with an installed Visual Studio C++ toolchain."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent

def main():
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("VERSION must contain a major.minor.patch version.")
    manifest = ROOT / "mod/mod.ini"
    text = manifest.read_text(encoding="utf-8")
    updated = re.sub(r"(?m)^version\s*=.*$", "version = " + version, text)
    if updated != text:
        manifest.write_text(updated, encoding="utf-8")
    where = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    found = subprocess.check_output([str(where), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not found:
        raise SystemExit("Visual Studio C++ build tools are required.")
    vs = Path(found)
    vcvars = vs / "VC/Auxiliary/Build/vcvars64.bat"
    # A fixed, tool-discovered command; no user-controlled command interpolation.
    output = subprocess.check_output(f'"{vcvars}" >nul && set', shell=True, text=True)
    env = {name.upper(): value for name, value in os.environ.items()}
    env.update((name.upper(), value) for name, value in
               (line.split("=", 1) for line in output.splitlines() if "=" in line))
    env["VSLANG"] = "1033"
    cmake = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    ninja = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    obj = ROOT / "build"
    subprocess.run([str(cmake), "-S", str(ROOT), "-B", str(obj), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_MAKE_PROGRAM={ninja}"], env=env, check=True)
    subprocess.run([str(cmake), "--build", str(obj)], env=env, check=True)
    target = ROOT / "mod/native/windows-x64"
    target.mkdir(parents=True, exist_ok=True)
    source, destination = obj / "pragmicat_mix.dll", target / "pragmicat_mix.dll"
    if not destination.is_file() or source.read_bytes() != destination.read_bytes():
        shutil.copy2(source, destination)
        print(f"Built {destination}")
    else:
        print(f"Unchanged: {destination}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
