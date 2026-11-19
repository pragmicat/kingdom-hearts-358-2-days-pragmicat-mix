"""Compile and execute the actual plugin against a bounded mock native host."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def compile_key(dependencies, inputs, command, environment, tools):
    """Include the compiler's actual transitive headers, not guessed includes."""
    paths = set(inputs + tools)
    for dependency in dependencies:
        data = json.loads(dependency.read_text(encoding='utf-8-sig'))['Data']
        if data.get('ImportedModules') or data.get('ImportedHeaderUnits'):
            raise ValueError('Module dependencies need a fresh compilation')
        paths.add(Path(data['Source']))
        paths.update(Path(name) for name in data['Includes'])
    digest = hashlib.sha256(json.dumps({'command': command, 'environment': {
        name: environment.get(name, '') for name in ('INCLUDE','LIB','LIBPATH','PATH','CL','_CL_','LINK','_LINK_')
    }}, sort_keys=True).encode())
    for path in sorted(paths, key=lambda p: str(p.resolve()).casefold()):
        digest.update(str(path.resolve()).encode('utf-8'))
        digest.update(b'\0' + hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def unchanged(cache, executable, key):
    try:
        record = json.loads(cache.read_text(encoding='utf-8'))
        return record['input_sha256'] == key and record['output_sha256'] == hashlib.sha256(executable.read_bytes()).hexdigest()
    except (OSError, ValueError, KeyError, TypeError):
        return False


def fixture():
    atlas = json.loads((ROOT/'generated/roxas_atlas.json').read_text(encoding='utf-8'))
    lines = ['// Generated from the independently parsed original ci/cm/wp atlas.',
             '#pragma once', 'struct FixtureNode { int kind, row, group, pool, index, id; };',
             'static const FixtureNode fixture_nodes[] = {']
    for char in atlas['characters']:
        kind = 0 if char['code'] == 'ro' else 19
        for weapon in char['weapons']:
            row = char['representative_wp_rows_by_graph'].get(str(weapon['index']))
            if row is None:
                continue
            for g, graph in enumerate(weapon['graphs']):
                for p, pool in enumerate(graph['pools']):
                    for node in pool['nodes']:
                        lines.append(' {'+', '.join(map(str,[kind,row,g,p,node['index'],node['attack_id']]))+'},')
    lines += ['};','struct FixtureRecord { int kind, id; int words[15]; };','static const FixtureRecord fixture_records[] = {']
    for char in atlas['characters']:
        kind = 0 if char['code'] == 'ro' else 19
        first = {}
        for attack in char['attacks']:
            first.setdefault(attack['attack_id'], attack)
        for attack in first.values():
            lines.append(' {'+str(kind)+', '+str(attack['attack_id'])+', {'+', '.join(map(str,attack['header_raw']))+'}},')
    lines += ['};','struct FixtureContact { int kind, id, index, end; };','static const FixtureContact fixture_contacts[] = {']
    for char in atlas['characters']:
        kind = 0 if char['code'] == 'ro' else 19
        seen=set()
        for attack in char['attacks']:
            if attack['attack_id'] in seen:
                continue
            seen.add(attack['attack_id'])
            for contact in attack['contacts']:
                lines.append(' {'+', '.join(map(str,[kind,attack['attack_id'],contact['index'],contact['raw'][2]]))+'},')
    lines += ['};']
    path = ROOT/'tests/generated_fixture.h'
    content = '\n'.join(lines)+'\n'
    if not path.is_file() or path.read_text(encoding='utf-8') != content:
        path.write_text(content,encoding='utf-8')


def run_portable(names):
    """Run the same ROM-free suites with a local C++20 compiler, without caching."""
    compiler = shutil.which(os.environ.get('CXX', 'c++'))
    if not compiler:
        raise SystemExit('Install a C++20 compiler or set CXX to its executable path.')
    output = ROOT / 'tests/.build'
    output.mkdir(exist_ok=True)
    print('Portable mock-host checks; Windows UI, launcher and game runtime are not exercised.', flush=True)
    results = []
    failed = False
    for name in names:
        executable = output / (name + '_tests')
        command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-O0',
                   '-I' + str(ROOT / 'vendor/sdk/include'), '-I' + str(ROOT / 'generated'),
                   '-I' + str(ROOT / 'tests'), str(ROOT / ('tests/' + name + '_tests.cpp')),
                   '-o', str(executable)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(executable)], text=True, capture_output=True)
        report = result.stdout + result.stderr
        print(report, end='', flush=True)
        results.append(report)
        (ROOT / ('tests/' + name + '_results.txt')).write_text(report, encoding='utf-8')
        failed = failed or result.returncode != 0
    (ROOT / 'tests/test_results.txt').write_text('\n'.join(results), encoding='utf-8')
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    names = ['flow','limit','motion','rules','drive','animation','hud','practice','weapon_animation','launcher_layout']
    ap.add_argument('--only', choices=names+['all'],default='all')
    ap.add_argument('--rebuild', action='store_true', help='Recompile selected test executables even when unchanged')
    args = ap.parse_args()
    fixture()
    if os.name != 'nt':
        raise SystemExit(run_portable(names if args.only == 'all' else [args.only]))
    where = Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = Path(subprocess.check_output([str(where),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
    vcvars = vs/'VC/Auxiliary/Build/vcvars64.bat'
    text = subprocess.check_output(f'"{vcvars}" >nul && set',shell=True,text=True)
    env = {name.upper(): value for name, value in os.environ.items()}
    env.update((name.upper(), value) for name, value in
               (line.split('=', 1) for line in text.splitlines() if '=' in line))
    env['VSLANG']='1033'
    output = ROOT/'tests/.build'
    output.mkdir(exist_ok=True)
    compilers = sorted((vs/'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))
    if not compilers:
        raise SystemExit('No x64 C++ compiler found in Visual Studio')
    tools = [compilers[-1], compilers[-1].parent/'link.exe']
    results=[]
    status=0
    for name in (names if args.only=='all' else [args.only]):
        exe=output/(name+'_tests.exe')
        dependency=output/(name+'_dependencies.json')
        cache=output/(name+'_build.json')
        command = [str(compilers[-1]),'/nologo','/std:c++20','/EHsc','/W4','/Od',
                   '/I'+str(ROOT/'vendor/sdk/include'),'/I'+str(ROOT/'generated'),'/I'+str(ROOT/'tests'),
                   '/sourceDependencies',str(dependency),
                   str(ROOT/('tests/'+name+'_tests.cpp')),'/Fe:'+str(exe),'/Fo:'+str(output/(name+'_tests.obj'))]
        if name == 'hud':
            command += ['/link','user32.lib','gdi32.lib']
        inputs = [Path(__file__), ROOT/('tests/'+name+'_tests.cpp')]
        try:
            key = compile_key([dependency], inputs, command, env, tools)
        except (OSError, ValueError, KeyError, TypeError):
            key = None
        if not args.rebuild and key and unchanged(cache, exe, key):
            print(f'Reusing compiled {name} checks.')
        else:
            subprocess.run(command,env=env,check=True)
            key = compile_key([dependency], inputs, command, env, tools)
            cache.write_text(json.dumps({'input_sha256': key, 'output_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}), encoding='utf-8')
        result = subprocess.run([str(exe)],env=env,text=True,capture_output=True)
        print(result.stdout,end='')
        print(result.stderr,end='')
        results.append(result.stdout+result.stderr)
        (ROOT/('tests/'+name+'_results.txt')).write_text(result.stdout+result.stderr,encoding='utf-8')
        status=max(status,result.returncode)
    (ROOT/'tests/test_results.txt').write_text('\n'.join(results),encoding='utf-8')
    raise SystemExit(status)


if __name__=='__main__':
    main()
