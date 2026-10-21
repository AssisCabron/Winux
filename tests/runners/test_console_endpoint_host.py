import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Portable console endpoint invariants, not a Windows console equivalence test."""
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    root=Path(__file__).resolve().parents[2]
    source=root/'tests/host/console_endpoint_host.c';exe=root/'build/console_endpoint_host.exe'
    subprocess.run([str(root/'tools/zig-x86_64-windows-0.14.1/zig.exe'),'cc','-target',
                    'x86_64-windows-gnu','-std=c11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-function','-O2',str(source),'-o',str(exe)],check=True,timeout=180)
    run=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=10)
    paths=['src/nt/io/nt_file_core.h','src/console/console_line.h','tests/host/console_endpoint_host.c']
    result=dict(passed=True,checks=run.stdout.strip(),
                hashes={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
                scope='Host callbacks only; no hardware wait, PEB publication, Windows equivalence or guest execution')
    (root/'build/console-endpoint-host-test.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':
    main()
