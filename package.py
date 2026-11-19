"""Package the self-contained launcher; never bundle the base game or user data."""
from pathlib import Path
import hashlib
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parent


def main() -> int:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("VERSION must contain a major.minor.patch version.")
    launcher = ROOT / "launcher/build/Kingdom Hearts 358-2 Days - Pragmicat Mix.exe"
    files = {
        "Kingdom Hearts 358-2 Days - Pragmicat Mix.exe": launcher,
        "LICENSE": ROOT / "LICENSE",
        "licenses/native-sdk.txt": ROOT / "vendor/sdk/LICENSE",
        "licenses/artwork.txt": ROOT / "assets/NOTICE.txt",
    }
    for path in files.values():
        if not path.is_file():
            raise SystemExit(f"Required file is missing: {path.relative_to(ROOT)}. Build and verify the project first.")
    manifest = (ROOT / "mod/mod.ini").read_text(encoding="utf-8")
    if not re.search(r"^version\s*=\s*" + re.escape(version) + r"\s*$", manifest, re.MULTILINE):
        raise SystemExit("The mod manifest version differs from VERSION. Rebuild the project first.")
    output = ROOT / "dist" / f"Kingdom-Hearts-358-2-Days-Pragmicat-Mix-{version}-windows-x64.zip"
    output.parent.mkdir(exist_ok=True)
    direct = output.parent / "Kingdom-Hearts-358-2-Days-Pragmicat-Mix.exe"
    shutil.copy2(launcher, direct)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in files.items():
            archive.write(path, name)
        archive.writestr("START HERE.txt", "Kingdom Hearts 358/2 Days: Pragmicat Mix\n\n"
            "Open Kingdom Hearts 358-2 Days - Pragmicat Mix.exe, then choose Play 358/2 Days Campaign or 358/2 Days Combat Practice.\n"
            "First setup finds your existing native Days PC port 0.1.5, or asks you to select its game executable.\n"
            "It copies the required game files into a separate Kingdom Hearts 358/2 Days: Pragmicat Mix installation.\n"
            "Campaign imports a copy of your progress once. Practice starts fresh and stays separate.\n"
            "The launcher contains the combat mod and practice mode; keep this one EXE wherever you like.\n")
    print(f"Packaged: {output}")
    print(f"SHA256: {hashlib.sha256(output.read_bytes()).hexdigest()}")
    print(f"Direct download: {direct}")
    print("Contains the launcher, embedded mod and practice mode. First setup imports your own base game locally.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
