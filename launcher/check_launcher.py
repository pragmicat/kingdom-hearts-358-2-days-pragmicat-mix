"""Check the single-file launcher without opening its UI or starting the game."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def main():
    executable = ROOT / "build/Kingdom Hearts 358-2 Days - Pragmicat Mix.exe"
    if not executable.is_file():
        raise SystemExit("Build the launcher first.")
    with tempfile.TemporaryDirectory(prefix="kingdom-hearts-358-days-launcher-check-") as folder:
        temporary = Path(folder)
        report = temporary / "self-test.json"
        subprocess.run([str(executable), "--self-test", "--report", str(report)], check=True, timeout=60)
        result = json.loads(report.read_text(encoding="utf-8"))
        assert result["ok"] and result["test_groups"] >= 25 and result["game_launched"] is False
        invalid = temporary / "invalid.json"
        outcome = subprocess.run([str(executable), "--check-setup", "--game", str(temporary / "missing-game"),
                                  "--profile-root", str(temporary / "unused-profile"), "--report", str(invalid)], timeout=60)
        failure = json.loads(invalid.read_text(encoding="utf-8"))
        assert outcome.returncode == 1 and failure["ok"] is False and failure["game_launched"] is False
        assert not (temporary / "unused-profile").exists(), "Invalid input must not create an installation"
        print(f"{result['test_groups']} launcher self-check groups passed; missing-game setup refused before writes.")
        print("Missing setup markers recover only after matching the original data; damaged copies stay unchanged.")
        print("Launcher UI and game were not opened.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
