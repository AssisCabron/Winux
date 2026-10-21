import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
from check_file_records import check_file_records
from check_image_records import check_image_records
from check_codepage_records import check_codepage_records
"""Update the existing Winux VM to this ISO and test via paced PS/2 input.
Requires --restart because it powers off that disposable experimental VM.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')
p=argparse.ArgumentParser();p.add_argument('--restart',action='store_true');args=p.parse_args()
if not args.restart:p.error('--restart is required to replace the running experimental VM session')
root=Path(__file__).resolve().parents[2];build=root/'build'
vbox=Path('C:/Program Files/Oracle/VirtualBox/VBoxManage.exe')
vm='Winux';iso=build/'winux-0.38-x64.iso';log=build/'virtualbox-0.38-serial.log'
def call(*words):
    # Poweroff can finish before VirtualBox releases its session lock.
    for attempt in range(20):
        run=subprocess.run([str(vbox),*map(str,words)],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=30)
        if not run.returncode:return run.stdout
        if 'already locked for a session (or being unlocked)' not in run.stderr:break
        time.sleep(.5)
    raise RuntimeError(run.stderr or run.stdout)
def readlog():return log.read_text(encoding='utf-8',errors='replace') if log.exists() else ''
def waitfor(marker,offset=0):
    end=time.monotonic()+30
    while time.monotonic()<end:
        text=readlog()[offset:]
        if marker in text:return text
        time.sleep(.1)
    raise AssertionError(f'Missing {marker!r}; serial tail: {readlog()[-1000:]}')
def send(line,marker,wait_prompt=True):
    start=len(readlog())
    for c in line:
        call('controlvm',vm,'keyboardputstring',c)
        time.sleep(.04)
    call('controlvm',vm,'keyboardputscancode','1c','9c')
    text=waitfor(marker,start)
    if wait_prompt:waitfor('winux> ',start+len(line)+1)
    return text
result={'iso':str(iso),'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest(),'vm':vm,'passed':False,'checks':[]}
try:
    initial=call('showvminfo',vm,'--machinereadable')
    # Refuse another OS/media or an unrelated VM rather than resetting it.
    expected_uuid='8cbe4d52-0912-4a4d-bec1-b843887ac170'
    if f'UUID="{expected_uuid}"' not in initial:raise RuntimeError('VM identity does not match this project')
    if 'VMState="running"' in initial:
        call('controlvm',vm,'poweroff')
    elif 'VMState="poweroff"' not in initial:raise RuntimeError('Unsupported VM state; leave it unchanged')
    log.write_bytes(b'')
    call('storageattach',vm,'--storagectl','SATA','--port','1','--device','0','--type','dvddrive','--medium',iso)
    call('modifyvm',vm,'--uartmode1','file',log)
    call('startvm',vm,'--type','gui')
    waitfor('KERNEL: CONSOLE_READY')
    result['checks'].append('0.38 boot and console ready on existing Winux VM')
    send('run HelloWorld.exe','PE_BLOCKED: .NET CLR runtime is not implemented; entry not called.')
    result['checks'].append('user HelloWorld.exe rejected for missing .NET CLR; entry not executed')
    text=send('page-test','PAGING_TEST_OK')
    text=send('vm-test','VM_REGIONS_OK')
    assert 'VM_LARGE_OK: reserve 0xfd0000, commit 3 MiB, page data preserved' in text
    native_memory=json.loads((build/'windows-nt-memory-test.json').read_text())
    assert native_memory['passed'] and len(native_memory['lines'])==11
    text=send('vm-query-test','VM_QUERY_OK')
    assert [line.strip() for line in text.splitlines() if line.startswith('VM_QUERY ')]==native_memory['lines']
    result['checks'].append('11 memory query observations match local Windows NT API')
    result['checks'].append('virtual regions and transactional commit exhaustion rollback')
    result['checks'].append('own paging: virtual alias, TLB remap, permissions and executable-page transition')
    text=send('run runtime.exe','PE_EXEC_OK')
    for marker in ['CONTEXT_OK','HEAP_OK','TLS_OK','ENV_OK','UTF16_OK','UTF8_OK','RUNTIME_CONTRACTS_OK','PE: GS restored','PE: FP control restored','Program returned 0']:
        assert marker in text,marker
    result['checks'].append('paced PS/2 command executes same runtime.exe and ExitProcess returns zero with GS/FP restored')
    text=send('run hello.exe','PE_EXEC_OK');assert 'Program returned 0' in text
    result['checks'].append('second original PE executes after runtime context teardown')
    text=send('run dlltest.exe','PE_EXEC_OK')
    assert 'DLL_GRAPH_OK:' in text and 'Program returned 0' in text
    assert text.index('DLL_ATTACH: winuxrt.dll')<text.index('DLL_ATTACH: winuxbridge.dll')
    assert text.index('DLL_DETACH: winuxbridge.dll')<text.index('DLL_DETACH: winuxrt.dll')
    result['checks'].append('two original DLLs: init order, data/functions, ordinal imports, forwarders and dynamic lookup')
    text=send('run dllexit.exe','PE_EXEC_OK')
    assert 'DLL_EXIT_NOTIFY_OK' in text and 'DLL_EXIT_NOTIFY_BAD' not in text and 'Program returned 0' in text
    assert text.index('DLL_DETACH: winuxbridge.dll')<text.index('DLL_DETACH: winuxrt.dll')
    result['checks'].append('ExitProcess sends DLL_PROCESS_DETACH before restoring shell')
    text=send('run tlstest.exe','PE_EXEC_OK')
    assert 'STATIC_TLS_OK:' in text and 'STATIC_TLS_DETACH_OK' in text and 'Program returned 0' in text
    result['checks'].append('static TLS compiler accesses and callbacks')
    text=send('imports cmd.exe','Dependency check complete. Entry not called.')
    native_nt=json.loads((build/'windows-ntmemory-test.json').read_text())
    assert native_nt['passed'] and len(native_nt['lines'])==11
    nt_text=send('run ntmemory.exe','PE_EXEC_OK')
    assert 'NT_MEMORY_OK' in nt_text and 'Program returned 0' in nt_text
    assert [line.strip() for line in nt_text.splitlines() if line.startswith('NT_MEMORY ')]==native_nt['lines']
    result['checks'].append('original NT-import PE matches 11 Windows status/output records')
    stub_text=send('nt-stub-test','NT_STUB_OK')
    assert 'NT_STUB_EVIDENCE: original NTDLL bytes; 6 memory SYSCALLs; unknown service rejected; dedicated stack' in stub_text
    result['checks'].append('original NTDLL stubs execute memory SYSCALLs on dedicated stack')
    partial=json.loads((build/'windows-partial-release-probe.json').read_text())
    assert partial['passed'] and len(partial['lines'])==36
    partial_text=send('nt-partial-test','NT_PARTIAL_OK')
    assert [line.strip() for line in partial_text.splitlines() if line.startswith(('NT_PARTIAL ','NT_PARTIAL_PAGE '))]==partial['lines']
    result['checks'].append('partial release via original NTDLL matches 36 Windows records')
    native_info=json.loads((build/'windows-nt-info-probe.json').read_text())
    assert native_info['passed'] and len(native_info['lines'])==9
    info_text=send('nt-info-test','NT_INFO_OK')
    nls_text=send('nt-nls-test','NT_NLS_TEST_OK')
    context_text=send('process-context-test','PROCESS_CONTEXT_TEST_OK')
    from check_lock_records import check_lock_records
    check_lock_records(send('nt-lock-test','NT_LOCK_TEST_OK'),build)
    for _ in range(2):
        heap_text=send('nt-heap-bootstrap-test','NT_HEAP_BOOTSTRAP_OK')
        assert 'NT_HEAP_LIFECYCLE_OK' in heap_text and 'NT_HEAP_BOOTSTRAP resources 1,1,0' in heap_text
        from check_heap_records import check_heap_records
        check_heap_records(heap_text,build)
        from check_process_parameter_records import check_process_parameter_records
        check_process_parameter_records(heap_text,build)
    result['checks'].append('original Microsoft heap lifecycle twice and resources restored')
    from check_fls_records import check_fls_records
    for _ in range(2):check_fls_records(send('nt-fls-test','NT_FLS_TEST_OK'),build)
    for _ in range(2):
        cp_text=send('kb-codepage-test','KB_CP_TEST_OK')
        check_codepage_records(cp_text,build)
        assert 'KB_NLS_PERSISTENT_OK' in cp_text and 'KB_ENVIRONMENT_OK' in cp_text
    for _ in range(2):check_image_records(send('nt-image-test','NT_IMAGE_TEST_OK'),build)
    for _ in range(2):check_file_records(send('nt-file-test','NT_FILE_TEST_OK'),build)
    for _ in range(2):
        console_start=len(readlog())
        send('nt-console-test','NT_CONSOLE_WAITING',wait_prompt=False)
        time.sleep(.2)
        send('console-input','NT_CONSOLE_TEST_OK')
        console_text=readlog()[console_start:]
        assert 'NT_CONSOLE_READ 636f6e736f6c652d696e7075740d0a' in console_text
        assert 'NT_CONSOLE_LIFECYCLE_OK' in console_text
        assert 'NT_CONSOLE_PERSISTENT_OK' in console_text
    result['checks'].append('original Windows console I/O: delayed PS/2 input, partial reads, timer/IF and handles restored')
    ansi_fixture=json.loads((build/'ansi-strings-windows.json').read_text())
    assert ansi_fixture['acp']==1252 and ansi_fixture['ntdll_sha256']=='36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    for _ in range(2):
        startup_text=send('nt-startup-test','NT_STARTUP_TEST_OK')
        assert [line.strip() for line in startup_text.splitlines() if line.startswith('NT_ANSI ')]==ansi_fixture['rows']
        assert all(marker in startup_text for marker in ['NT_ANSI_FREE_OK','KB_VERSION_BASIC_OK','KB_STARTUP_INFO_OK','KB_INPUTS_PERSISTENT_OK','KB_INPUTS_ROLLBACK_OK','KB_PRIVATE_HEAP_OK'])
    native_text=send('nt-native-context-test','NT_NATIVE_CONTEXT_TEST_OK')
    assert 'NT_NATIVE_CONTEXT cycle 0,1,1,1' in native_text and 'NT_NATIVE_CONTEXT cycle 1,1,1,1' in native_text
    assert native_text.count('NT_PRIVATE_HEAP_GUARD_OK')==2
    parts=native_text.split('NT_PROCESS_PARAMS_OK:')
    assert len(parts)==3
    for part in parts[:2]:check_process_parameter_records(part+'NT_PROCESS_PARAMS_OK',build)
    assert 'PROCESS_CONTEXT_OK: PEB parameters, original normalization, console handles, synchronized environment, bounds, reset' in context_text
    assert 'NT_NLS_OK: original Init/Reset, ANSI 1252, OEM 850, persistent tables, null UTF-8' in nls_text
    assert 'NT_INFO_VM_OK: cross-page outputs, RO rejection, decommit, recommit, release' in info_text
    assert [line.strip() for line in info_text.splitlines() if line.startswith('NT_INFO ')]==native_info['lines']
    result['checks'].append('original NTDLL process/system queries match Windows status, size and normalized fields')
    from check_object_records import check_object_records
    check_object_records(send('nt-object-test','NT_OBJECT_OK'),build)
    result['checks'].append('47 event/namespace records through original NTDLL SYSCALLs match Windows')
    seed_text=send('nt-heap-seed-test','NT_HEAP_SEED_OK')
    assert 'NT_HEAP_SEED_EVIDENCE: original helpers returned; RunOnce state initialized; 2 process-query SYSCALLs' in seed_text
    result['checks'].append('original NTDLL heap helpers initialize RunOnce through process-cookie SYSCALLs')
    counts=re.findall(r'Module ([^ ]+) - Imports: (\d+); unresolved: (\d+)',text)
    assert sum(int(c[1]) for c in counts)==3405 and all(int(c[2])==0 for c in counts)
    assert 'Mapped modules: 8;' in text and 'Static TLS modules: 3;' in text and 'DLL_ATTACH:' not in text and 'PE: entering' not in text
    result['checks'].append('eight real Windows modules preflight: 3405 regular imports, zero unresolved, no entry called')
    send('cls','winux> ')
    send('echo Winux 0.38 - terminal','winux> ')
    send('help','help  cls  ver  mem  uptime  dir')
    call('controlvm',vm,'screenshotpng',build/'virtualbox-0.38.png')
    result['checks'].append('framebuffer screenshot captured; VM left running')
    result['passed']=True
except Exception as error:
    result['failure']=f'{type(error).__name__}: {error}'
(build/'virtualbox-0.38-test.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2));sys.exit(0 if result['passed'] else 1)

