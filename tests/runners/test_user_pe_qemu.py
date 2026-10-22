"""QEMU proof that a PE32+ file on FAT16 executes in a managed CPL3 process."""
from pathlib import Path
import json,hashlib,socket,subprocess,shutil,time,codecs,sys,re
root=Path(__file__).resolve().parents[2];build=root/'build'
tag='user-pe64'
iso=build/'user-pe-preview'/'winux-0.38-wrapper-fls-candidate.iso'
disk=build/'user-pe-preview'/'user-pe-fat16-v2.img'
fixture=json.loads((build/'user-pe-preview'/'user-pe-fixture-v2.json').read_text(encoding='utf-8'))
assert iso.is_file() and disk.is_file()
assert hashlib.sha256(disk.read_bytes()).hexdigest()==fixture['disk_sha256']
vars=build/(tag+'-vars.fd')
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',vars)
with socket.socket() as s:
    s.bind(('127.0.0.1',0));port=s.getsockname()[1]
command=[str(root/'tools/qemu/qemu-system-x86_64.exe'),'-machine','q35','-accel','tcg','-m','256M','-smp','1',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={vars}',
 '-cdrom',str(iso),'-boot','d','-display','none','-nic','none','-no-reboot',
 '-serial',f'tcp:127.0.0.1:{port},server=on,wait=off',
 '-drive',f'if=none,id=user-pe64-disk,file={disk},format=raw,readonly=on',
 '-device','virtio-blk-pci,drive=user-pe64-disk,disable-modern=on']
output='';checks=[];failure=None;dec=codecs.getincrementaldecoder('utf-8')('replace')
start=time.monotonic()
with (build/(tag+'-stderr.log')).open('wb') as log:
    process=subprocess.Popen(command,stdout=log,stderr=log)
    sock=None
    try:
        for i in range(100):
            try:
                sock=socket.create_connection(('127.0.0.1',port),timeout=1)
                break
            except OSError:time.sleep(.1)
        if sock is None:raise TimeoutError('QEMU serial unavailable')
        sock.settimeout(.3)
        def collect(marker,frompos,secs=15):
            global output
            limit=time.monotonic()+secs
            while marker not in output[frompos:]:
                if time.monotonic()>limit or process.poll() is not None:
                    raise AssertionError(f'Missing {marker}: '+output[frompos:][-1700:])
                try:
                    chunk=sock.recv(65536)
                    if not chunk:raise RuntimeError('QEMU serial closed')
                    output+=dec.decode(chunk)
                except socket.timeout:pass
            return output[frompos:]
        collect('KERNEL: CONSOLE_READY',0,45)
        checks.append('UEFI kernel booted')
        def send(cmd,marker):
            begin=len(output);sock.sendall((cmd+'\r').encode())
            return collect(marker,begin)
        for cycle in range(3):
            text=send('exec APPS/USER64.EXE','USER_PE_LIFECYCLE_OK')
            assert 'USER_PE_EXEC_OK: external PE32+ on CPL3 via FAT16' in text,text
            assert 'PROCESS_EXIT code=0 fault=0' in text,text
            assert 'PROCESS_REAP_OK' in text,text
        checks.append('three external FAT16 PE32+ CPL3 runs with syscall, exit and full reaping')
        bad=send('exec APPS/BAD64.EXE','USER_PE_REJECTED: malformed or unsupported PE32+')
        assert 'PROCESS_CREATED' not in bad
        unknown=send('exec APPS/UNKNOWN.EXE','USER_PE_REJECTED: unknown/untrusted path')
        assert 'PROCESS_CREATED' not in unknown
        checks.append('malformed and unknown PE paths rejected before process creation')
        for name in ('WRITE64.EXE','NX64.EXE'):
            trace=send('exec APPS/'+name,'USER_PE_SECURITY_OK')
            assert 'USER_PE_FAULT_CONTAINED_OK' in trace and 'PROCESS_REAP_OK' in trace,trace
            assert 'PROCESS_EXIT code=98 fault=14' in trace,trace
        checks.append('CPL3 write-to-RX and execute-NX faults isolated and reaped')
        again=send('exec APPS/USER64.EXE','USER_PE_LIFECYCLE_OK')
        assert 'PROCESS_REAP_OK' in again
        checks.append('normal PE execution survives recovered malicious user faults')
    except Exception as e:failure=f'{type(e).__name__}: {e}'
    finally:
        (build/(tag+'-serial.log')).write_text(output,encoding='utf-8')
        if sock:sock.close()
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill()
result={'passed':failure is None,'failure':failure,'seconds':round(time.monotonic()-start,2),
 'checks':checks,'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest(),
 'fat16_sha256':hashlib.sha256(disk.read_bytes()).hexdigest()}
(build/(tag+'-test.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
sys.exit(0 if result['passed'] else 1)
