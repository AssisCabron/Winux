import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
from _paths import analysis_cmd_dir
from check_file_records import check_file_records
from check_image_records import check_image_records
from check_codepage_records import check_codepage_records
"""Diskless QEMU integration: serial, PS/2, RAM files, PE validation and timer."""
from pathlib import Path
import argparse, codecs, hashlib, json, re, shutil, socket, subprocess, sys, time
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')

root=Path(__file__).resolve().parents[2]
build=root/'build'
parser=argparse.ArgumentParser()
parser.add_argument('--fault',action='store_true')
parser.add_argument('--page-fault',action='store_true')
parser.add_argument('--nx-fault',action='store_true')
parser.add_argument('--iso',type=Path,help='Override ISO under the project root or provide an absolute path')
parser.add_argument('--result-tag',default='console-wrapper-fls-candidate',help='Safe output prefix')
parser.add_argument('--check-nt-missing',action='store_true',help='Check bounded unknown NT syscall trace')
parser.add_argument('--check-plugin',action='store_true',help='Exercise trusted custom Windows DLL and host fixture')
parser.add_argument('--block-image',type=Path,help='Read-only expendable block fixture located under build/')
parser.add_argument('--check-proc-space',action='store_true',help='Execute two CPL3 tasks in independent CR3 address spaces')
parser.add_argument('--check-scheduler',action='store_true',help='Verify real Win64 stack/CR3 context switching and cooperative yields')
parser.add_argument('--check-user-scheduler',action='store_true',help='Require two CPL3 process continuations to resume after native yield')
parser.add_argument('--check-services',action='store_true',help='Verify resident CPL3 clockd autostart, scheduling, listing, stop and restart')
parser.add_argument('--check-vfs',action='store_true',help='Verify FAT16 read-only mount, root listing and file contents')
parser.add_argument('--check-preemption',action='store_true',help='Assert PIT IRQ0 interrupts two CPU-bound non-yielding CPL3 programs')
parser.add_argument('--check-process-manager',action='store_true',help='Verify repeated process spawn, exit, resource reaping and free-page invariants')
args=parser.parse_args()
assert re.fullmatch(r'[a-zA-Z0-9-]{1,70}',args.result_tag), 'Invalid result tag'
tag='page-fault' if args.page_fault else ('nx-fault' if args.nx_fault else ('fault' if args.fault else args.result_tag))
def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0))
        return s.getsockname()[1]
serial_port,qmp_port=free_port(),free_port()
qemu=root/'tools/qemu/qemu-system-x86_64.exe'
iso=(root/args.iso if args.iso and not args.iso.is_absolute() else args.iso) if args.iso else build/'winux-0.38-wrapper-fls-candidate.iso'
assert iso.is_file(),f'ISO missing: {iso}'
variables=build/f'{tag}-vars.fd'
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',variables)
command=[str(qemu),'-machine','q35','-accel','tcg','-m','256M','-smp','1',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={variables}',
 '-cdrom',str(iso),'-boot','d','-display','none','-nic','none','-no-reboot',
 '-serial',f'tcp:127.0.0.1:{serial_port},server=on,wait=off',
 '-qmp',f'tcp:127.0.0.1:{qmp_port},server=on,wait=off']
if args.block_image:
    block_image=(root/args.block_image if not args.block_image.is_absolute() else args.block_image).resolve()
    assert block_image.is_file() and (root/'build') in block_image.parents and block_image.suffix.lower()=='.img'
    permitted={'driver-test-disk.img':4096,'fat16-test-disk.img':32768}
    assert block_image.name in permitted and block_image.stat().st_size==permitted[block_image.name]*512
    command += ['-drive',f'if=none,id=winux-block-test,file={block_image},format=raw,readonly=on',
                '-device','virtio-blk-pci,drive=winux-block-test,disable-modern=on']
output='';checks=[];start=time.monotonic();failure=None
decoder=codecs.getincrementaldecoder('utf-8')('replace')
def connect(port):
    until=time.monotonic()+10
    while time.monotonic()<until:
        try:return socket.create_connection(('127.0.0.1',port),timeout=1)
        except OSError:time.sleep(.1)
    raise TimeoutError('QEMU socket unavailable')
with (build/f'{tag}-stderr.log').open('w') as stderr:
    process=subprocess.Popen(command,stdout=stderr,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
    try:
        serial=connect(serial_port);serial.settimeout(.2)
        qmp=connect(qmp_port);qmp.settimeout(10);qfile=qmp.makefile('rb')
        json.loads(qfile.readline())
        def execute(name,arguments=None):
            qmp.sendall((json.dumps({'execute':name,'arguments':arguments or {}})+'\n').encode())
            while True:
                reply=json.loads(qfile.readline())
                if 'error' in reply:raise RuntimeError(reply)
                if 'return' in reply:return reply['return']
        execute('qmp_capabilities')
        def wait_for(text,offset=0,timeout=15):
            global output
            until=time.monotonic()+timeout
            while text not in output[offset:]:
                if time.monotonic()>until or process.poll() is not None:raise AssertionError(f'Missing {text!r}')
                try:
                    chunk=serial.recv(65536)
                    if not chunk:raise RuntimeError('serial disconnected')
                    output+=decoder.decode(chunk)
                except socket.timeout:pass
            return output[offset:]
        def send(line,expected):
            before=len(output);serial.sendall((line+'\r').encode());return wait_for(expected,before)
        wait_for('KERNEL: CONSOLE_READY',timeout=45)
        if args.block_image:
            for cycle in range(2):
                block_text=send('block-test','BLOCK_TEST_OK')
                assert 'PNP_DMA_GUARD_OK: PCI exact match' in block_text
                assert 'BLOCK_READ_OK: post-UEFI PCI legacy VirtIO' in block_text
                assert 'PNP_DEVICE_LIFECYCLE_OK: detach/reprobe' in block_text
                expected_sectors=32768 if args.check_vfs else 4096
                assert f'driver=virtio-blk-legacy sectors={expected_sectors} sector_size=512' in block_text
            checks.append('two post-UEFI PCI VirtIO read-only cycles with driver detachment, DMA cleanup and rebinding')
        if args.check_vfs:
            assert args.block_image and block_image.name=='fat16-test-disk.img'
            mounted=send('fs-test','VFS_TEST_OK')
            assert 'VFS_FAT16_OK: readonly mount, 2 root files' in mounted
            listing=send('lsdisk','VFS_LIST_END')
            assert 'VFS_FILE HELLO.TXT bytes=900' in listing
            assert 'VFS_FILE STATUS.TXT bytes=28' in listing
            text=send('catdisk status.txt','VFS_READ_OK')
            assert 'WINUX_FAT16_RUNTIME_READ_OK' in text
            assert 'VFS_NOT_FOUND' in send('catdisk ../HELLO.TXT','VFS_NOT_FOUND')
            assert 'VFS_NOT_FOUND' in send('catdisk MISSING.TXT','VFS_NOT_FOUND')
            checks.append('FAT16 readonly mount using real PCI block driver, 900-byte scattered file chain, root enumeration, path rejection')
            # Removing the PCI function must invalidate the live VFS mount.
            again=send('block-test','BLOCK_TEST_OK')
            assert 'PNP_DEVICE_LIFECYCLE_OK' in again
            assert 'VFS_NOT_FOUND' in send('catdisk STATUS.TXT','VFS_NOT_FOUND')
            remount=send('fs-test','VFS_TEST_OK')
            assert 'VFS_FAT16_OK:' in remount
            assert 'WINUX_FAT16_RUNTIME_READ_OK' in send('catdisk STATUS.TXT','VFS_READ_OK')
            checks.append('block device detach invalidates VFS mount and remount restores FAT16 file access')
        if args.check_process_manager:
            for cycle in range(2):
                record=send('process-test','PROC_MANAGER_TEST_OK')
                assert 'PROC_MANAGER_OK: 12 CPL3 process creates/runs/exits/reaps' in record
                assert 'pool fully restored' in record
            checks.append('24 independent CPL3 process launches with unique PIDs, per-process NT handles and complete physical/page-table reaping')
        if args.check_preemption:
            preempt_text=send('preempt-test','PREEMPT_TEST_OK')
            assert 'PIT_PREEMPT_OK: IRQ32 forced two non-yielding CPL3 loops' in preempt_text
            assert 'PIT_THREAD_STACKS_OK: separate privileged RSP0' in preempt_text
            assert 'PIT_THREAD_FPU_OK: distinct CPL3 MXCSR values preserved' in preempt_text
            match=re.search(r'PIT_PREEMPT_STATS: interrupts=(\d+) context_switches=(\d+)',preempt_text)
            assert match and int(match.group(1))>=2 and int(match.group(2))==3
            checks.append('hardware PIT IRQ32 forced two CPU-bound CPL3 programs to resume with their own saved contexts')
        if args.check_scheduler:
            for cycle in range(2):
                sched_text=send('sched-test','SCHED_TEST_OK')
                assert 'SCHED_CONTEXT_OK: 2 task stacks' in sched_text
                assert '2 CR3, 6 yields, exit teardown' in sched_text
                assert 'SCHED_OBJECT_NAMESPACE_OK: 2 independent NT handle tables' in sched_text
                assert 'SCHED_FPU_FLAGS_OK: task-specific MXCSR' in sched_text
                if args.check_user_scheduler:
                    assert 'CPL3_SCHED_OK: 2 processes resumed after native yield' in sched_text
            if args.check_user_scheduler:
                checks.append('two successive cycles of two CPL3 processes yield/resume in isolated CR3 roots')
            checks.append('two cycles of cooperative kernel context switching, FPU, handles and teardown')
        if args.check_proc_space:
            for cycle in range(2):
                proc_text=send('proc-space-test','PROC_TEST_OK')
                assert 'PROC_SPACE_OK: 2 independent CR3 roots' in proc_text
                assert 'CPL3 exits 21/42/21' in proc_text
            checks.append('two cycles of independent user CR3/page tables, same VA, distinct frames and safe teardown')
        if args.check_services:
            first=send('services','SERVICE_LIST_END')
            matched=re.search(r'SERVICE clockd pid=401 state=RUNNING generation=(\d+) quanta=(\d+) last_pit_tick=(\d+)',first)
            assert matched,first[-1500:]
            generation=int(matched.group(1))
            first_runs=int(matched.group(2))
            first_ticks=int(matched.group(3))
            listing=send('ps','PROCESS_LIST_END')
            assert 'PROCESS pid=401 name=clockd cpl=3 state=RUNNING cr3=' in listing
            initial_cr3=re.search(r'cr3=(0x[0-9a-f]{16})',listing)
            assert initial_cr3
            page_report=send('mem','free pages, 4 KiB each')
            initial_free=re.search(r'Reserved kernel pool: (\d+) / (\d+) free pages',page_report)
            assert initial_free
            assert 'SERVICE_START_REJECTED' in send('svc start clockd','SERVICE_START_REJECTED')
            time.sleep(.75)
            second=send('services','SERVICE_LIST_END')
            observed=re.search(r'SERVICE clockd pid=401 state=RUNNING generation=(\d+) quanta=(\d+) last_pit_tick=(\d+)',second)
            assert observed and int(observed.group(1))==generation
            assert int(observed.group(2))>first_runs,(first,second)
            assert int(observed.group(3))>first_ticks,(first,second)
            checks.append('resident CPL3 clockd runs in shell idle, queries real PIT ticks and appears in ps/services')
            send('svc stop clockd','SERVICE_STOP_OK')
            stopped=send('services','SERVICE_LIST_END')
            assert re.search(r'SERVICE clockd pid=0 state=STOPPED generation=(\d+)',stopped)
            assert 'PROCESS pid=401' not in send('ps','PROCESS_LIST_END')
            send('svc stop clockd','SERVICE_STOP_FAILED')
            send('svc restart clockd','SERVICE_RESTART_OK')
            restarted=send('services','SERVICE_LIST_END')
            state=re.search(r'SERVICE clockd pid=401 state=RUNNING generation=(\d+) quanta=(\d+)',restarted)
            assert state and int(state.group(1))==generation+1
            restarted_listing=send('ps','PROCESS_LIST_END')
            assert 'cr3='+initial_cr3.group(1) in restarted_listing
            current_pool=send('mem','free pages, 4 KiB each')
            free_after=re.search(r'Reserved kernel pool: (\d+) / (\d+) free pages',current_pool)
            assert free_after and free_after.groups()==initial_free.groups(),(page_report,current_pool)
            time.sleep(.75)
            active=send('services','SERVICE_LIST_END')
            current=re.search(r'SERVICE clockd pid=401 state=RUNNING generation=(\d+) quanta=(\d+)',active)
            assert current and int(current.group(2))>=1
            send('svc stop clockd','SERVICE_STOP_OK')
            assert 'state=STOPPED' in send('services','SERVICE_LIST_END')
            checks.append('CPL3 service stop/restart/reap reuses process memory and removes exited PID from ps')
            checks.append('service lifecycle rejects invalid transitions and retains CR3/page-pool counts across restart')
            # memd must be a second REAL CPL3 resident process. Its service
            # syscall reads the existing physical page allocator, not a
            # fabricated value or a duplicate memory manager.
            mem_report=send('services','SERVICE_LIST_END')
            mem_match=re.search(r'SERVICE memd pid=402 state=RUNNING generation=(\d+) quanta=(\d+) last_free_pages=(\d+)',mem_report)
            assert mem_match and int(mem_match.group(2))>0,mem_report
            memory=send('mem','free pages, 4 KiB each')
            available=re.search(r'Reserved kernel pool: (\d+) / (\d+) free pages',memory)
            assert available and int(mem_match.group(3))==int(available.group(1)),(mem_report,memory)
            mem_ps=send('ps','PROCESS_LIST_END')
            mem_cr3=re.search(r'PROCESS pid=402 name=memd cpl=3 state=RUNNING cr3=(0x[0-9a-f]{16})',mem_ps)
            assert mem_cr3 and mem_cr3.group(1)!=initial_cr3.group(1)
            assert 'SERVICE_UNKNOWN' in send('svc start not-installed','SERVICE_UNKNOWN')
            send('svc stop memd','SERVICE_STOP_OK')
            assert 'PROCESS pid=402' not in send('ps','PROCESS_LIST_END')
            send('svc restart memd','SERVICE_RESTART_OK')
            restarted_mem=send('ps','PROCESS_LIST_END')
            assert 'cr3='+mem_cr3.group(1) in restarted_mem
            send('svc stop memd','SERVICE_STOP_OK')
            checks.append('two distinct CPL3 services: independent PIDs/CR3, live memory syscall, stop/restart and unknown-service rejection')
        assert 'AFTER_EXIT_BOOT_SERVICES' in output and '[OK] Reserved page allocator' in output
        checks.append('post-firmware console and allocator self-test')
        assert 'KERNEL: OWN_CR3_ACTIVE WP NX' in output
        checks.append('own CR3 installed with write-protect and NX enabled')
        if args.page_fault or args.nx_fault:
            send('page-fault-test' if args.page_fault else 'nx-fault-test','CR2=0x0000000100000000')
            assert 'FAULT vector=0x000000000000000e' in output
            assert ('error=0x0000000000000003' if args.page_fault else 'error=0x0000000000000011') in output
            checks.append('CPU rejects read-only write' if args.page_fault else 'CPU rejects NX execution')
        elif args.fault:
            send('fault-test','KERNEL: FAULT vector=0x0000000000000006')
            checks.append('UD2 reaches own exception vector 6 without reset')
        else:
            text=send('run HelloWorld.exe','PE_BLOCKED: .NET CLR runtime is not implemented; entry not called.')
            assert 'PE_EXEC_OK' not in text
            checks.append('user HelloWorld.exe found and rejected specifically for missing .NET CLR; entry not executed')
            send('page-test','PAGING_TEST_OK')
            assert 'VM_LARGE_OK: reserve 0xfd0000, commit 3 MiB, page data preserved' in send('vm-test','VM_REGIONS_OK')
            native_memory=json.loads((build/'windows-nt-memory-test.json').read_text())
            assert native_memory['passed'] and len(native_memory['lines'])==11
            text=send('vm-query-test','VM_QUERY_OK')
            observed=[line.strip() for line in text.splitlines() if line.startswith('VM_QUERY ')]
            assert observed==native_memory['lines'],(observed,native_memory['lines'])
            checks.append('11 memory query observations match local Windows NtQueryVirtualMemory across reserve, commit, protect, decommit and release')
            checks.append('virtual regions reserve/commit, zeroing, recommit preservation, protection, exhaustion rollback and release')
            checks.append('CPU virtual alias, TLB remap, permissions and executable-page transition')
            send('help','pe-test  fault-test');checks.append('serial command input')
            send('ls','sample.pe');send('cat welcome.txt','Files in this volume');checks.append('RAM file listing and reading')
            send('cat missing.txt','ERROR: file not found');checks.append('missing-file rejection')
            send('run hello.wxs','Reading files and dispatching commands now works.')
            wait_for('Ticks (100 Hz):');checks.append('script execution')
            send('alloc-test','ALLOC_TEST_OK');checks.append('pool exhaustion, reuse, zeroing and double-free rejection')
            send('pe sample.pe','PE_OK AMD64 PE32+');send('pe welcome.txt','PE_REJECTED');checks.append('PE fixture accepted and non-PE rejected')
            send('pe-test','PE_TEST_OK');checks.append('truncated and out-of-bounds PE rejected')
            send('run sample.pe','ERROR: run accepts .wxs or .exe files');checks.append('unsupported execution suffix rejected explicitly')
            text=send('uptime','; idle wakes:')
            t1=int(re.search(r'Ticks \(100 Hz\): (\d+)',text).group(1))
            time.sleep(1.2)
            text=send('uptime','; idle wakes:')
            t2=int(re.search(r'Ticks \(100 Hz\): (\d+)',text).group(1))
            assert t2>t1+50,(t1,t2)
            checks.append('PIT interrupts advance ticks while idle')
            send('not-a-command','ERROR: unknown command');checks.append('unknown-command rejection')
            # Wait for the previous prompt before using the independent PS/2 input route.
            send('echo serial-end','serial-end\r\n')
            before=len(output)
            for key in ['e','c','h','o','spc','k','e','y','o','k','ret']:
                execute('send-key',{'keys':[{'type':'qcode','data':key}],'hold-time':30})
                time.sleep(.08)
            wait_for('keyok\r\n',before)
            checks.append('emulated PS/2 keyboard command')
            send('clear','winux> ')
            send('about','General Windows applications and drivers: not supported.')
            send('ls','sample.pe')
            send('cat welcome.txt','The real CMD is not supported yet.')
            send('run hello.exe','PE_EXEC_OK')
            checks.append('original Win32 PE maps, relocates, resolves three imports and returns zero')
            text=send('run runtime.exe','PE_EXEC_OK')
            for marker in ['CONTEXT_OK:','HEAP_OK:','TLS_OK:','ENV_OK:','UTF16_OK:','UTF8_OK:','TIMER_OK:','RUNTIME_CONTRACTS_OK','PE: GS restored','PE: FP control restored']:
                assert marker in text,marker
            assert 'a\u00e7\u00e3o, caf\u00e9, \u03a9, \U0001f680' in text
            checks.append('Win32 runtime: dedicated stack, GS/IDs, per-thread error, heap/realloc, TLS, environment, UTF-16, timers and ExitProcess')
            text=send('run dlltest.exe','PE_EXEC_OK')
            assert 'DLL_GRAPH_OK:' in text and 'Program returned 0' in text
            assert text.index('DLL_ATTACH: winuxrt.dll')<text.index('DLL_ATTACH: winuxbridge.dll')
            assert text.index('DLL_DETACH: winuxbridge.dll')<text.index('DLL_DETACH: winuxrt.dll')
            checks.append('DLL dependency graph: DllMain order, function/data imports, ordinal imports, forwarded exports and dynamic lookup')
            assert 'DLL_EXIT_NOTIFY_OK' in text and 'DLL_EXIT_NOTIFY_BAD' not in text and 'DLL_STATIC_TLS_DETACH_OK' in text
            text=send('run dllexit.exe','PE_EXEC_OK')
            assert 'DLL_EXIT_NOTIFY_OK' in text and 'DLL_EXIT_NOTIFY_BAD' not in text and 'DLL_STATIC_TLS_DETACH_OK' in text
            assert text.index('DLL_DETACH: winuxbridge.dll')<text.index('DLL_DETACH: winuxrt.dll')
            assert 'Program returned 0' in text and 'PE: FP control restored' in text
            checks.append('ExitProcess notifies both DLLs in reverse dependency order before returning to shell')
            if args.check_plugin:
                host=json.loads((build/'plugin-preview'/'plugin-windows-test.json').read_text(encoding='utf-8'))
                assert host['passed'] and host['exit_code']==0
                guest_manifest=json.loads((build/'plugin-preview'/'ramfs-manifest.json').read_text(encoding='utf-8'))
                efi=(build/'plugin-preview'/'BOOTX64-wrapper-fls.EFI').read_bytes()
                for name in ('plugintest.exe','winuxplugin.dll'):
                    entry=next(m for m in guest_manifest if m['name']==name)
                    payload=(build/'plugin-preview'/name).read_bytes()
                    assert hashlib.sha256(payload).hexdigest()==entry['sha256']
                    assert payload in efi
                dll_text=send('run plugintest.exe','PE_EXEC_OK')
                for marker in ('WINUX_CUSTOM_DLL_OK:','DLL_ATTACH: winuxplugin.dll','DLL_DETACH: winuxplugin.dll','Program returned 0'):
                    assert marker in dll_text,marker
                assert 'PE_EXEC_FAILED' not in dll_text
                checks.append('unchanged custom Windows DLL+EXE execute on guest: DllMain, data, exports, dynamic lookup, GetTickCount, output and detach')
            text=send('run tlstest.exe','PE_EXEC_OK')
            assert 'STATIC_TLS_OK:' in text and 'STATIC_TLS_DETACH_OK' in text and 'Program returned 0' in text
            checks.append('static TLS template, zero initialization, compiler GS access and process callbacks')
            send('tls-test','TLS_BOUNDS_OK')
            text=send('run tlstest.exe','PE_EXEC_OK')
            assert 'STATIC_TLS_OK:' in text and 'STATIC_TLS_DETACH_OK' in text
            checks.append('TLS rejects reversed template, out-of-range index/callback, oversized zero fill and invalid alignment; reset succeeds')
            native_nt=json.loads((build/'windows-ntmemory-test.json').read_text())
            assert native_nt['passed'] and len(native_nt['lines'])==11
            for repeat in range(2):
                text=send('run ntmemory.exe','PE_EXEC_OK')
                assert 'NT_MEMORY_OK' in text and 'Program returned 0' in text
                assert [line.strip() for line in text.splitlines() if line.startswith('NT_MEMORY ')]==native_nt['lines']
            send('alloc-test','ALLOC_TEST_OK')
            checks.append('same original NT-import PE matches 11 Windows status/output records twice and releases physical pages')
            manifest=json.loads((build/'ramfs-manifest.json').read_text())
            native=json.loads((build/'windows-runtime-test.json').read_text())
            runtime=next(f for f in manifest if f['name']=='runtime.exe')
            assert native['passed'] and native['sha256']==runtime['sha256']
            executable=(build/'runtime.exe').read_bytes()
            assert hashlib.sha256(executable).hexdigest()==runtime['sha256']
            assert executable in (build/'BOOTX64.EFI').read_bytes()
            for name in ['hello','runtime','dlltest','dllexit','tlstest','ntmemory']:
                native=json.loads((build/f'windows-{name}-test.json').read_text())
                assert native['passed']
                hashes={f'{name}.exe':native['sha256'],**native.get('dependencies',{})}
                for filename,digest in hashes.items():
                    data=(build/filename).read_bytes()
                    assert hashlib.sha256(data).hexdigest()==digest
                    assert next(f for f in manifest if f['name']==filename)['sha256']==digest
                    assert data in (build/'BOOTX64.EFI').read_bytes()
            checks.append('identical six EXEs and two DLLs passed on Windows and are embedded in tested kernel')
            if any(f['name']=='ntdll.dll' for f in manifest):
                if args.check_nt_missing:send('nt-missing-reset','NT_MISSING_RESET_OK')
                for repeat in range(2):
                    stub_text=send('nt-stub-test','NT_STUB_OK')
                    assert 'NT_STUB_EVIDENCE: original NTDLL bytes; 6 memory SYSCALLs; unknown service rejected; dedicated stack' in stub_text
                checks.append('original mapped NTDLL stubs execute six memory SYSCALLs twice on dedicated stack; unknown service rejected')
                if args.check_nt_missing:
                    missing_text=send('nt-missing','NT_MISSING_END')
                    assert 'NT_MISSING_BEGIN unique=1 overflow=0' in missing_text
                    assert 'NT_MISSING_SERVICE 4294967295 calls=2' in missing_text
                    checks.append('unknown NT syscall ID logged twice without changing STATUS_NOT_IMPLEMENTED')
                partial=json.loads((build/'windows-partial-release-probe.json').read_text())
                assert partial['passed'] and len(partial['lines'])==36
                partial_text=send('nt-partial-test','NT_PARTIAL_OK')
                assert [line.strip() for line in partial_text.splitlines() if line.startswith(('NT_PARTIAL ','NT_PARTIAL_PAGE '))]==partial['lines']
                checks.append('partial release via original NTDLL: 36 Windows observations, preserved bytes, metadata exhaustion and heap-sized prefix trim')
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
                checks.append('original Microsoft heap lifecycle twice, zeroed allocation, exact size, empty registry and resources restored')
                from check_fls_records import check_fls_records
                for _ in range(2):check_fls_records(send('nt-fls-test','NT_FLS_TEST_OK'),build)
                for _ in range(2):
                    cp_text=send('kb-codepage-test','KB_CP_TEST_OK')
                    check_codepage_records(cp_text,build)
                    assert 'KB_NLS_PERSISTENT_OK' in cp_text and 'KB_ENVIRONMENT_OK' in cp_text
                for _ in range(2):check_image_records(send('nt-image-test','NT_IMAGE_TEST_OK'),build)
                for _ in range(2):check_file_records(send('nt-file-test','NT_FILE_TEST_OK'),build)
                for _ in range(2):
                    start_console=len(output)
                    send('nt-console-test','NT_CONSOLE_WAITING')
                    time.sleep(.2)
                    serial.sendall(b'console-input\r')
                    console_text=wait_for('NT_CONSOLE_TEST_OK',start_console)
                    assert 'NT_CONSOLE_READ 636f6e736f6c652d696e7075740d0a' in console_text
                    assert 'NT_CONSOLE_LIFECYCLE_OK' in console_text
                    assert 'NT_CONSOLE_PERSISTENT_OK' in console_text
                checks.append('original Windows console I/O: delayed serial input, partial line reads, timer/IF and handles restored')
                ansi_fixture=json.loads((build/'ansi-strings-windows.json').read_text())
                assert ansi_fixture['acp']==1252 and ansi_fixture['ntdll_sha256']=='36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
                for _ in range(2):
                    startup_text=send('nt-startup-test','NT_STARTUP_TEST_OK')
                    assert 'NT_HEAP_POLICY_OK:' in startup_text, startup_text[-2500:]
                    assert 'NT_LFH_ALLOCATION_OK:' in startup_text, startup_text[-2500:]
                    assert "KB_NT_REALLOC_OK:" in startup_text and "KB_HEAP_SET_FAILURES: 0,122,0,31" in startup_text, startup_text[-2500:]
                    assert "KB_FLS_WRAPPERS_OK:" in startup_text and "KB_FLS_TEARDOWN_OK:" in startup_text, startup_text[-2500:]
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
                checks.append('original NTDLL process-cookie/system-basic queries: 9 Windows status, size and normalized field observations')
                from check_object_records import check_object_records
                check_object_records(send('nt-object-test','NT_OBJECT_OK'),build)
                checks.append('47 event/namespace records through original NTDLL SYSCALLs match Windows')
                seed_text=send('nt-heap-seed-test','NT_HEAP_SEED_OK')
                assert 'NT_HEAP_SEED_EVIDENCE: original helpers returned; RunOnce state initialized; 2 process-query SYSCALLs' in seed_text
                checks.append('original NTDLL heap random helpers return and initialize RunOnce through two process-cookie SYSCALLs')
            if any(f['name']=='ntdll.dll' for f in manifest):
                text=send('imports cmd.exe','Dependency check complete. Entry not called.')
                counts=re.findall(r'Module ([^ ]+) - Imports: (\d+); unresolved: (\d+)',text)
                assert counts and sum(int(c[1]) for c in counts)==3405 and all(int(c[2])==0 for c in counts),counts
                assert 'Mapped modules: 8;' in text and 'Static TLS modules: 3;' in text
                assert 'PE: entering' not in text and 'DLL_ATTACH:' not in text
                expected=json.loads((analysis_cmd_dir(root)/'dependency-startup.json').read_text())
                efi=(build/'BOOTX64.EFI').read_bytes()
                for m in expected['modules']:
                    f=next(f for f in manifest if f['name']==m['name'])
                    data=Path(f['source']).read_bytes()
                    assert hashlib.sha256(data).hexdigest()==f['sha256']==m['sha256']
                    assert data in efi
                checks.append('eight original Windows modules mapped and regular imports resolved without executing entry points; source hashes verified')
                text=send('run cmd.exe','PE_BLOCKED: execution currently limited to bundled original contract tests.')
                assert 'PE: entering' not in text and 'DLL_ATTACH:' not in text
                checks.append('real CMD remains non-executable pending NT environment and TLS support')
            elif any(f['name']=='cmd.exe' for f in manifest):
                text=send('run cmd.exe','PE_BLOCKED: required APIs are not implemented; entry not called.')
                counts=re.search(r'Imports: (\d+); unresolved: (\d+)',text)
                assert counts and int(counts[1])==271 and 0<int(counts[2])<268
                checks.append('actual local CMD blocked before entry for unsupported imports')
            send('run hello.exe','PE_EXEC_OK')
            checks.append('demo runs again after arena reuse')
            text=send('run runtime.exe','PE_EXEC_OK')
            assert 'RUNTIME_CONTRACTS_OK' in text and 'PE: GS restored' in text
            checks.append('runtime context, heap, TLS and environment reset across repeated invocations')
            time.sleep(.3)
            send('cls','winux> ')
            send('echo Winux 0.38 - terminal','winux> ')
            help_start=len(output)
            send('help','echo <text>  alloc-test  pe-test  fault-test')
            wait_for('winux> ',help_start+len('help')+2)
            time.sleep(.2)
            execute('screendump',{'filename':str(build/'console-wrapper-fls-candidate.ppm')})
            # QEMU produces raw PPM; avoid optional Pillow for this isolated suite.
            ppm=(build/'console-wrapper-fls-candidate.ppm').read_bytes()
            assert ppm.startswith(b'P6') and len(ppm)>100000, 'Invalid framebuffer capture'
            checks.append('framebuffer screenshot captured')
    except Exception as error:
        failure=f'{type(error).__name__}: {error}'
    finally:
        if process.poll() is None:process.terminate()
        process.wait(timeout=10)
(build/f'{tag}-serial.log').write_text(output,encoding='utf8')
result={'passed':failure is None,'failure':failure,'seconds':round(time.monotonic()-start,2),
 'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest(),'command':command,'checks':checks,
 'not_tested':['general Windows applications and drivers','user mode','user-mode page isolation','physical hardware','USB keyboard','SMP']}
(build/f'{tag}-test.json').write_text(json.dumps(result,indent=2))
print(output[-10000:]);print(json.dumps(result,indent=2))
sys.exit(0 if failure is None else 1)
