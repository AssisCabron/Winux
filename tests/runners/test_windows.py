import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Run the original PE contract binaries on the Windows host for comparison."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,time
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')
build=Path(__file__).resolve().parents[2]/'build'
from _paths import analysis_cmd_dir
sys.path.insert(0,str(analysis_cmd_dir(build.parent)))
from dependency_graph import PE
def tls_codegen(name):
    pe=PE(build/name);r,n=pe.dirs[9];assert r and n>=40
    t=pe.off(r,40);template=pe.u64(t+8)-pe.u64(t)
    code=b''.join(pe.get(x['raw'],x['raw_size']) for x in pe.sections if x['flags']&0x20000000)
    # x64 MOV r64, GS:[0x58]: require the emitted instruction in code, not a log string.
    accesses=len(re.findall(bytes.fromhex('65')+b'[\x48-\x4f]'+bytes.fromhex('8b')+b'.'+bytes.fromhex('2558000000'),code))
    assert template>=8 and accesses>0,(name,template,accesses)
    return dict(template_bytes=template,gs_tls_loads=accesses,sha256=hashlib.sha256(pe.b).hexdigest())
records=[]
for name in ['hello','runtime','dlltest','dllexit','tlstest','ntmemory']:
    binary=build/f'{name}.exe'
    start=time.monotonic()
    if name=='runtime':
        # WriteConsoleW requires an actual console, not a redirected pipe.
        startup=subprocess.STARTUPINFO()
        startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow=0
        run=subprocess.run([str(binary)],startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE,timeout=20)
        capture={'console':'new Windows console, hidden window; output not captured'}
    else:
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        capture={'stdout':run.stdout,'stderr':run.stderr}
    record={'binary':str(binary),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'exit_code':run.returncode,'passed':run.returncode==0,
            'seconds':round(time.monotonic()-start,2),**capture}
    if name in ['dlltest','dllexit']:
        record['passed']=record['passed'] and 'DLL_GRAPH_OK: init order, functions, data, ordinals, forwarders, dynamic exports' in run.stdout
        record['passed']=record['passed'] and 'DLL_EXIT_NOTIFY_OK' in run.stdout and 'DLL_EXIT_NOTIFY_BAD' not in run.stdout and (name!='dllexit' or 'DLL_STATIC_TLS_DETACH_OK' in run.stdout)
        record['dependencies']={n:hashlib.sha256((build/n).read_bytes()).hexdigest() for n in ['winuxrt.dll','winuxbridge.dll']}
    if name=='tlstest':record['passed']=record['passed'] and 'STATIC_TLS_OK:' in run.stdout and 'STATIC_TLS_DETACH_OK' in run.stdout
    if name=='ntmemory':
        record['lines']=[line for line in run.stdout.splitlines() if line.startswith('NT_MEMORY ')]
        record['passed']=record['passed'] and 'NT_MEMORY_OK' in run.stdout and len(record['lines'])==11
    if name in ['dlltest','dllexit','tlstest']:record['tls_codegen']=tls_codegen('tlstest.exe' if name=='tlstest' else 'winuxrt.dll')
    (build/f'windows-{name}-test.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    records.append(record)
print(json.dumps(records,indent=2))
sys.exit(0 if all(r['passed'] for r in records) else 1)
