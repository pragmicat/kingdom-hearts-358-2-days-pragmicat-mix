#!/usr/bin/env python3
"""Patch pinned source in a temporary tree; execute real upstream sound as Wasm."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
PIN = json.loads((HERE / 'MANIFEST.json').read_text(encoding='utf-8'))
SOUND = ['archive', 'bus', 'categories', 'channels', 'mix_ds', 'mix_pc', 'render', 'replace',
         'snd7_alarm', 'snd7_bank', 'snd7_channel', 'snd7_command', 'snd7_driver', 'snd7_hw',
         'snd7_os', 'snd7_seq', 'snd7_tables', 'stream_reader', 'third_party_decoders']


def dependency(directory: Path, name: str) -> None:
    pin = PIN['dependencies'][name]
    source = directory / pin['file']
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != pin['sha256']:
        raise ValueError(f"Use pinned {name} {pin['commit']}: {pin['file']} differs")


def prepare(upstream: Path, output: Path) -> None:
    # Read the recorded public commit, not an arbitrary dirty worktree. Never
    # mutate the caller's source or import generated game/ROM/build files.
    for relative, digest in PIN['base_sha256'].items():
        data = subprocess.check_output(['git', '-C', str(upstream), 'show', PIN['upstream_commit'] + ':' + relative])
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f'Pinned upstream sound source differs: {relative}')
    archive = subprocess.check_output(['git', '-C', str(upstream), 'archive', PIN['upstream_commit'], 'port'])
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:') as source:
        source.extractall(output, filter='data')
    subprocess.run(['git', 'apply', '--check', str(HERE / 'runtime-sound.patch')], cwd=output, check=True)
    subprocess.run(['git', 'apply', str(HERE / 'runtime-sound.patch')], cwd=output, check=True)


def run(upstream: Path, stb: Path, dr_libs: Path, emcc: str, node: str) -> None:
    dependency(stb, 'stb')
    dependency(dr_libs, 'dr_libs')
    version = subprocess.check_output([emcc, '--version'], text=True).splitlines()[0]
    if not re.search(r'\b' + re.escape(PIN['emscripten']) + r'\b', version):
        raise ValueError(f"Use Emscripten {PIN['emscripten']}; found {version}")
    with tempfile.TemporaryDirectory(prefix='days-real-sound-wasm-') as directory:
        work = Path(directory)
        prepare(upstream, work)
        port = work / 'port'
        common = [emcc, '-std=c17', '-O2', '-fdeclspec', '-Wall', '-Wextra',
                  '-sASSERTIONS=2', '-sSTACK_OVERFLOW_CHECK=2', '-sALLOW_MEMORY_GROWTH=1',
                  '-I', str(stb), '-I', str(dr_libs)]
        executable = work / 'sound-test.js'
        subprocess.run([
            *common, *(str(port / 'sound' / (name + '.c')) for name in SOUND),
            str(port / 'runtime/sound/sound_bridge.c'), str(port / 'runtime/sound/sound_mods.c'),
            str(port / 'tests/sound_test.c'), str(HERE / 'tests/session_log_fixture.c'),
            '-o', str(executable),
        ], check=True)
        subprocess.run([node, str(executable)], cwd=work, check=True)
        executable = work / 'replacement-queue.js'
        subprocess.run([
            *common, '-sASYNCIFY', '-I', str(port / 'runtime/sound'),
            str(port / 'sound/replace.c'), str(port / 'sound/third_party_decoders.c'),
            str(HERE / 'tests/replacement_queue_smoke.c'), '-o', str(executable),
        ], check=True)
        subprocess.run([node, str(executable)], cwd=work, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--stb', type=Path, required=True)
    parser.add_argument('--dr-libs', type=Path, required=True)
    parser.add_argument('--emcc', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    run(args.upstream.resolve(), args.stb.resolve(), args.dr_libs.resolve(), args.emcc, args.node)
