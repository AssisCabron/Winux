import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Exercise actual NT adapter buffers against retained Windows I/O records."""
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2]
    oracle = json.loads((root/'build/file-io-windows.json').read_text())
    assert oracle['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    lines = ['static const struct scenario cases[]={']
    for r in oracle['records']:
        payload = bytes.fromhex(r['payload'])
        assert len(payload) <= 32 and r['count'] <= 32
        fields = [json.dumps(r['step']), str(int(r['operation']=='write')), str(r['count']),
                  str(int(r['offset'] is not None)), str(r['offset'] or 0),
                  '{'+(','.join(map(str,payload)) or '0')+'}']
        lines.append('{'+','.join(fields)+'},')
    lines.append('};')
    (root/'tests/fixtures/file_io_cases.h').write_text('\n'.join(lines)+'\n')
    exe = root/'build/nt_file_api_host.exe'
    subprocess.run([str(root/'tools/zig-x86_64-windows-0.14.1/zig.exe'), 'cc',
                    '-target', 'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', '-O2', str(root/'tests/host/nt_file_api_host.c'),
                    '-o', str(exe)], check=True, timeout=180)
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True, timeout=10)
    expected = [{k:r[k] for k in ('step','status','iosb','buffer')} for r in oracle['records']]
    actual = [json.loads(s) for s in run.stdout.splitlines()]
    assert len(actual)==14 and actual==expected, (actual,expected)
    result = dict(passed=True, records=len(actual), invariants=run.stderr.strip(),
                  adapter_sha256=hashlib.sha256((root/'src/nt/io/nt_file_api.h').read_bytes()).hexdigest(),
                  test_sha256=hashlib.sha256((root/'tests/host/nt_file_api_host.c').read_bytes()).hexdigest(),
                  fixture_sha256=hashlib.sha256((root/'build/file-io-windows.json').read_bytes()).hexdigest(),
                  scope='Host adapter; full IOSB and buffers for these synchronous cases only; no ISO or console')
    (root/'build/nt-file-api-host-test.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
