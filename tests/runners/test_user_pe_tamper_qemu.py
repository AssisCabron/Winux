"""Fail-closed test for tampered FAT16 executable, isolated guest."""
from pathlib import Path
import hashlib,json,shutil,socket,subprocess,time,codecs,sys
root=Path(__file__).resolve().parents[2];build=root/'build'
tag='user-pe64-tamper'
disk=build/'user-pe-preview'/'user-pe-tampered-fat16-v2.img'
manifest=json.loads((build/'user-pe-preview'/'user-pe-tampered-fixture-v2.json').read_text())
iso=build/'user-pe-preview'/'winux-0.38-wrapper-fls-candidate.iso'
assert hashlib.sha256(disk.read_bytes()).hexdigest()==manifest['sha256']
vars=build/(tag+'-vars.fd')
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',vars)
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
cmd=[str(root/'tools/qemu/qemu-system-x86_64.exe'),'-machine','q35','-accel','tcg','-m','256M','-smp','1',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={vars}',
 '-cdrom',str(iso),'-boot','d','-display','none','-nic','none','-no-reboot',
 '-serial',f'tcp:127.0.0.1:{port},server=on,wait=off',
 '-drive',f'if=none,id=altered,file={disk},format=raw,readonly=on',
 '-device','virtio-blk-pci,drive=altered,disable-modern=on']
output='';dec=codecs.getincrementaldecoder('utf8')('replace');failure=None;checks=[]
with (build/(tag+'-stderr.log')).open('wb') as log:
    p=subprocess.Popen(cmd,stdout=log,stderr=log)
    sock=None
    try:
        for _ in range(100):
            try:sock=socket.create_connection(('127.0.0.1',port),timeout=1);break
            except OSError:time.sleep(.1)
        if sock is None:raise RuntimeError('serial port unavailable')
        sock.settimeout(.25)
        def until(marker,begin,deadline=20):
            global output
            end=time.monotonic()+deadline
            while marker not in output[begin:]:
                if time.monotonic()>end or p.poll() is not None:
                    raise AssertionError(f'Missing {marker}: '+output[begin:][-1500:])
                try:
                    part=sock.recv(65536)
                    if not part:raise RuntimeError('QEMU socket closed')
                    output+=dec.decode(part)
                except socket.timeout:pass
            return output[begin:]
        until('KERNEL: CONSOLE_READY',0,45)
        start=len(output)
        sock.sendall(b'exec APPS/USER64.EXE\r')
        a=until('USER_PE_REJECTED: file integrity mismatch',start)
        assert 'PROCESS_CREATED' not in a and 'USER_PE_EXEC_OK' not in a
        checks.append('altered executable from FAT16 rejected before Process Manager spawn')
        start=len(output)
        sock.sendall(b'exec APPS/NX64.EXE\r')
        b=until('USER_PE_SECURITY_OK',start)
        assert 'PROCESS_REAP_OK' in b
        checks.append('legitimate separate executable still runs and faults are contained')
    except Exception as e:failure=f'{type(e).__name__}: {e}'
    finally:
        (build/(tag+'-serial.log')).write_text(output,encoding='utf8')
        if sock:sock.close()
        p.terminate()
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.kill()
result={'passed':failure is None,'failure':failure,'checks':checks,
        'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest(),
        'tampered_disk_sha256':hashlib.sha256(disk.read_bytes()).hexdigest()}
(build/(tag+'-test.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result,indent=2))
sys.exit(0 if result['passed'] else 1)
