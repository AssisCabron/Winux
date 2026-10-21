"""Check the retained host fixture's device-query contract before guest use."""
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2] / 'build'
    d = json.loads((root/'volume-device-windows.json').read_text())
    assert d['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    assert len(d['records']) == 32
    devices = {'disk': (7, 1), 'nul': (0x15, 2), 'pipe-read': (0x11, 3), 'pipe-write': (0x11, 3)}
    seen = set()
    for r in d['records']:
        key = (r['handle'], r['size'])
        assert key not in seen
        seen.add(key)
        iosb, output = bytes.fromhex(r['iosb']), bytes.fromhex(r['output'])
        if r['size'] < 8:
            assert r['status'] == 0xc0000004
        elif r['handle'] in devices:
            device, kind = devices[r['handle']]
            assert r['status'] == 0 and r['file_type'] == kind and r['last_error'] == 0x1234
            assert int.from_bytes(output[:4], 'little') == device
            assert iosb[:4] == bytes(4) and iosb[4:8] == bytes([0xa5])*4
            assert int.from_bytes(iosb[8:], 'little') == 8
            assert output[8:] == bytes([0xa5])*16
        else:
            expected = 0xc0000024 if r['handle'] in ('event', 'process-pseudo') else 0xc0000008
            assert r['status'] == expected and r['file_type'] == 0 and r['last_error'] == 6
        if r['status']:
            assert iosb == bytes([0xa5])*16 and output == bytes([0xa5])*24
    assert len(seen) == 32
    report = dict(passed=True, records=32, scope='Host fixture; no guest execution, console or async I/O')
    (root/'volume-device-fixture-test.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
