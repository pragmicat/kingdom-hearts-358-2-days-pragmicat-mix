"""Check the actual built Windows plugin without starting the game or its UI."""
from pathlib import Path
import ctypes
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]


class Descriptor(ctypes.Structure):
    _fields_ = [('size', ctypes.c_uint32), ('major', ctypes.c_uint16),
                ('minor', ctypes.c_uint16), ('name', ctypes.c_char_p),
                ('start', ctypes.c_void_p), ('stop', ctypes.c_void_p)]


def verify_resources(path, version):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(path.resolve()), None, 0x22)  # Resource access only.
    assert module, f'Cannot read Windows resources: {path.name}'
    try:
        assert kernel.FindResourceW(module, 101, 14), f'Heart icon missing: {path.name}'
        assert kernel.FindResourceW(module, 1, 16), f'Version metadata missing: {path.name}'
    finally:
        kernel.FreeLibrary(module)
    data = path.read_bytes()
    assert 'Kingdom Hearts 358/2 Days: Pragmicat Mix'.encode('utf-16le') in data
    assert (version + '\0').encode('utf-16le') in data


def embedded_resource(path, identifier):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.SizeofResource.restype = ctypes.c_uint32
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(path.resolve()), None, 0x22)
    assert module, 'Cannot inspect the standalone launcher'
    try:
        resource = kernel.FindResourceW(module, identifier, 10)
        assert resource, f'Embedded payload {identifier} is missing'
        size = kernel.SizeofResource(module, resource)
        handle = kernel.LoadResource(module, resource)
        pointer = kernel.LockResource(handle)
        assert size and pointer, f'Embedded payload {identifier} is empty'
        return ctypes.string_at(pointer, size)
    finally:
        kernel.FreeLibrary(module)


def main():
    version = (ROOT/'VERSION').read_text(encoding='utf-8').strip()
    dll = ROOT / 'mod/native/windows-x64/pragmicat_mix.dll'
    launcher = ROOT / 'launcher/build/Kingdom Hearts 358-2 Days - Pragmicat Mix.exe'
    for path in (dll, launcher):
        data = path.read_bytes()
        assert data[:2] == b'MZ', f'Not a Windows executable: {path.name}'
        offset = int.from_bytes(data[0x3c:0x40], 'little')
        assert data[offset:offset+4] == b'PE\0\0'
        assert int.from_bytes(data[offset+4:offset+6], 'little') == 0x8664, 'Expected x64'
        verify_resources(path, version)
    payload = {200: dll, 201: ROOT/'mod/mod.ini', 202: ROOT/'mod/parameters.ini',
               203: ROOT/'practice/mod.ini', 204: ROOT/'practice/scripts/main.lua',
               205: ROOT/'practice/missions/missions.txt', 206: ROOT/'LICENSE',
               207: ROOT/'vendor/sdk/LICENSE', 208: ROOT/'assets/NOTICE.txt', 209: ROOT/'assets/kingdom-hearts-358-2-days-pragmicat-mix-logo.png'}
    for identifier, source in payload.items():
        assert embedded_resource(launcher, identifier) == source.read_bytes(), f'Embedded payload differs: {source.name}'
    library = ctypes.CDLL(str(dll.resolve()))
    query = library.kh_plugin_query
    query.argtypes = []
    query.restype = ctypes.POINTER(Descriptor)
    descriptor = query().contents
    assert descriptor.size == ctypes.sizeof(Descriptor)
    assert (descriptor.major, descriptor.minor) == (1, 0)
    assert descriptor.name == b'Kingdom Hearts 358/2 Days: Pragmicat Mix' and descriptor.start and descriptor.stop
    start = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_void_p)(descriptor.start)
    assert start(None) == -2, 'Invalid host must be refused before hooks or UI'
    marker = '\nARTIFACT VALIDATION\n'
    results = (ROOT/'tests/test_results.txt').read_text(encoding='utf-8').split(marker)[0]
    assert 'FAIL ' not in results and '0 failed' in results
    count = sum(line.startswith('PASS ') for line in results.splitlines())
    manifest = (ROOT/'mod/mod.ini').read_text(encoding='utf-8')
    assert re.search(r'^version\s*=\s*' + re.escape(version) + r'\s*$', manifest, re.MULTILINE)
    report = {'version': version, 'passed_groups': count,
              'windows_x64_artifacts': True, 'release_dll_load': True,
              'sdk_abi': '1.0', 'null_host_safely_refused': True,
              'heart_icons_and_product_metadata': True,
              'standalone_launcher_payloads_match_source': True,
              'game_launched': False, 'launcher_launched': False,
              'files': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in (dll, launcher)}}
    (ROOT/'tests/release_verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    (ROOT/'tests/test_results.txt').write_text(results + marker +
        'PASS actual x64 DLL loads and exposes the expected SDK descriptor\n'
        'PASS actual release start refuses an invalid host before UI/hooks\n'
        'Game and launcher were not launched. Combat feel is not verified by these checks.\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
