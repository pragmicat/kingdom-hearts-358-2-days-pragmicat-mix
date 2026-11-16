"""Verify dynamic native names/routes in the real PE without launching it."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def check(runtime):
    data = runtime.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    machine, count = struct.unpack_from('<HH', data, pe+4)
    assert machine == 0x8664
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b
    base = struct.unpack_from('<Q', data, optional+24)[0]
    sections = []
    for n in range(count):
        at = optional+optional_size+n*40
        name = data[at:at+8].rstrip(b'\0').decode('ascii')
        virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, at+8)
        sections.append((name, address, raw_size, raw, virtual_size))

    def cstring(pointer):
        rva = pointer-base
        for _, address, size, raw, _ in sections:
            if address <= rva < address+size:
                at = raw+rva-address
                end = data.find(b'\0', at, min(len(data), at+256))
                if end >= at:
                    return data[at:end].decode('ascii', errors='strict')
        return None

    route_names = set()
    for name, _, size, raw, _ in sections:
        if name.startswith('.khrt'):
            for at in range(raw, raw+size-31, 32):
                pointer, route, body, _ = struct.unpack_from('<QQQQ', data, at)
                if pointer and route and body:
                    symbol = cstring(pointer)
                    if symbol:
                        route_names.add(symbol)
    assert route_names, 'Native routine replacement table was not found'
    names, required_routes = set(), {'func_ov022_020a3c78', 'Ov002_LoadAnimTables',
                                    'Ov002_ApplyAnimMode', 'Ov002_FreeResourceTables'}
    prefixes = ('Ov', 'func_', 'NNS', 'Heap_', 'gOv', 'gParty', 'data_',
                'GameState_', 'LoadGlobal', 'Slot_', 'SNDi_', 'VeneerTo_', 'InstantiateClass',
                'Archive_', 'Resource_', 'FreeAllResourceTables')
    for source in (ROOT/'src').glob('*.cpp'):
        text = source.read_text(encoding='utf-8')
        names.update(n for n in re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', text) if n.startswith(prefixes))
        names.update(re.findall(r'routine\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"', text))
        names.update(re.findall(r'->datum\(\s*"([A-Za-z_][A-Za-z0-9_]*)"', text))
        required_routes.update(re.findall(r'replace\(\s*"([A-Za-z_][A-Za-z0-9_]*)"', text))
        required_routes.update(re.findall(r'replace\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"', text))
    absent = sorted(n for n in names if n.encode()+b'\0' not in data)
    missing_routes = sorted(required_routes-route_names)
    assert not absent, ('Missing native names', absent)
    assert not missing_routes, ('Missing replacement routes', missing_routes)
    return {'native_names_checked': len(names), 'required_routes_checked': len(required_routes),
            'all_native_names_present': True, 'all_required_routes_present': True,
            'runtime_sha256': hashlib.sha256(data).hexdigest(), 'game_launched': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runtime', type=Path)
    args = parser.parse_args()
    report = check(args.runtime)
    (ROOT/'tests/native_runtime_verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
