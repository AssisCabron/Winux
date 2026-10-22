"""Isolated Intel VT-d DMA fault regression (no legacy full-suite overhead)."""
from pathlib import Path
import subprocess, socket, shutil, time, json, hashlib, codecs, sys
root=Path(__file__).resolve().parents[2]
build=root/'build'
iso=build/'vtd-preview'/'winux-0.38-wrapper-fls-candidate.iso'
disk=build/'vfs-preview'/'fat16-tree-disk.img'
assert iso.is_file() and disk.is_file()
mode=sys.argv[1] if len(sys.argv)>1 else 'fault'
assert mode in ('fault','invalidate','positive'),mode
tag={'fault':'vtd-hardware-fault','invalidate':'vtd-iotlb-invalidation','positive':'vtd-modern-positive'}[mode]
vars=build/(tag+'-vars.fd')
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',vars)
def freeport():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
port=freeport()
cmd=[str(root/'tools/qemu/qemu-system-x86_64.exe'),'-machine','q35','-accel','tcg','-m','256M','-smp','1',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={vars}',
 '-cdrom',str(iso),'-boot','d','-display','none','-nic','none','-no-reboot',
 '-serial',f'tcp:127.0.0.1:{port},server=on,wait=off',
 '-device','intel-iommu,aw-bits=48,caching-mode=on',
 '-trace',f'events={root}/tests/runners/vtd_trace_events,file={build}/{tag}-trace.log',
 '-drive',f'if=none,id=vtd-test,file={disk},format=raw,readonly=on',
 '-device','virtio-blk-pci,drive=vtd-test,disable-legacy=on,iommu_platform=on']
output='';decoder=codecs.getincrementaldecoder('utf8')('replace');checks=[];failure=None;start=time.monotonic()
with (build/(tag+'-stderr.log')).open('wb') as log:
    process=subprocess.Popen(cmd,stdout=log,stderr=log)
    try:
        serial=None
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:serial=socket.create_connection(('127.0.0.1',port),timeout=1);break
            except OSError:time.sleep(.1)
        if serial is None:raise RuntimeError('Serial unavailable')
        serial.settimeout(.2)
        def waitfor(marker,timeout=40):
            global output
            until=time.monotonic()+timeout
            while marker not in output:
                if time.monotonic()>until or process.poll() is not None:
                    raise AssertionError(f'Missing marker: {marker}')
                try:
                    chunk=serial.recv(65536)
                    if not chunk:raise RuntimeError('serial disconnected')
                    output+=decoder.decode(chunk)
                except socket.timeout:pass
        def send(command,marker,timeout=40):
            global output
            position=len(output)
            serial.sendall((command+'\r').encode())
            until=time.monotonic()+timeout
            while marker not in output[position:]:
                if time.monotonic()>until or process.poll() is not None:
                    raise AssertionError(f'Missing {marker} after {command}: '+output[position:][-1200:])
                try:
                    chunk=serial.recv(65536)
                    if not chunk:raise RuntimeError('serial disconnected')
                    output+=decoder.decode(chunk)
                except socket.timeout:pass
            return output[position:]
        waitfor('KERNEL: CONSOLE_READY')
        assert 'VTD_DMAR_OK' in output
        checks.append('DRHD detected via ACPI')
        a=send('vtd-status','faults=0')
        assert 'present=1' in a
        checks.append('VT-d enumerated and initially disabled')
        if mode=='positive':
            for cycle in range(2):
                b=send('block-test','BLOCK_TEST_OK',timeout=40)
                assert 'VTD_MODERN_DMA_OK: ACCESS_PLATFORM' in b,b
            checks.append('Four read-only VirtIO 1.0 domain attach/read/detach/reprobe cycles')
        elif mode=='fault':
            b=send('vtd-fault-test','VTD_FAULT_TEST_OK',timeout=90)
            assert 'VTD_MODERN_HW_FAULT_OK' in b and 'VTD_FAULT_TEST_OK' in b,b
            checks.append('Unauthorized PCI DMA blocked and hardware fault recorded')
        else:
            b=send('vtd-invalidate-test','VTD_INVALIDATION_TEST_OK',timeout=12)
            assert 'VTD_IOTLB_INVALIDATION_OK' in b,b
            checks.append('Cached IOTLB permission revoked, faulted, re-enabled and reprobed')
        c=send('vtd-status','faults='+('0' if mode=='positive' else '1'))
        assert 'enabled=1' in c and 'domains=0' in c,c
        checks.append('Translation stays on after detach, no domain remains')
    except Exception as e:
        failure=f'{type(e).__name__}: {e}'
    finally:
        (build/(tag+'-serial.log')).write_text(output,encoding='utf8')
        if serial:
            try:serial.close()
            except Exception:pass
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill()
trace_file=build/(tag+'-trace.log')
if failure is None:
    trace_text=trace_file.read_text(encoding='utf8',errors='replace') if trace_file.exists() else ''
    if 'vtd_dmar_translate' not in trace_text:
        failure='QEMU trace missing actual translated device DMA'
    elif mode in ('fault','invalidate') and 'vtd_dmar_fault' not in trace_text:
        failure='QEMU trace missing hardware IOMMU fault'
    elif mode=='positive' and 'vtd_dmar_fault' in trace_text:
        failure='Unexpected QEMU remapping fault during authorized I/O'
    else:
        checks.append('QEMU vtd_dmar_translate trace and fault/no-fault status independently verified')
result={'passed':failure is None,'failure':failure,'seconds':round(time.monotonic()-start,2),
 'checks':checks,'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest()}
(build/(tag+'-test.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result,indent=2))
sys.exit(0 if result['passed'] else 1)
