import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Run only a diskless QEMU VM. Stop this child after BOOT_OK or a 45 s timeout."""
from pathlib import Path
import subprocess, time, json, sys, shutil, hashlib
root=Path(__file__).resolve().parents[2]
qemu=root/'tools/qemu/qemu-system-x86_64.exe'
log=root/'build/serial.log'
log.write_text('')
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',root/'build/OVMF_VARS.fd')
command=[str(qemu),'-machine','q35','-accel','tcg','-m','256M','-smp','1',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={root}/build/OVMF_VARS.fd',
 '-cdrom',str(root/'build/winux-0.38-x64.iso'),'-boot','d',
 '-display','none','-serial',f'file:{log}','-monitor','none','-nic','none','-no-reboot']
start=time.monotonic()
creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0
with (root/'build/qemu-stderr.log').open('w') as err:
 p=subprocess.Popen(command,stderr=err,stdout=err,creationflags=creationflags)
 try:
  while time.monotonic()-start<45 and p.poll() is None:
   time.sleep(.25)
   if 'KERNEL: BOOT_OK' in log.read_text(errors='replace'):break
  output=log.read_text(errors='replace')
  success='KERNEL: AFTER_EXIT_BOOT_SERVICES' in output and 'KERNEL: BOOT_OK' in output
 finally:
  if p.poll() is None:p.terminate()
  p.wait(timeout=10)
result={'passed':success,'seconds':round(time.monotonic()-start,2),
 'iso_sha256':hashlib.sha256((root/'build/winux-0.38-x64.iso').read_bytes()).hexdigest(),
 'qemu_version':subprocess.check_output([str(qemu),'--version'],text=True).splitlines()[0], 'command':command,
 'checks':['UEFI ISO boot','ExitBootServices returned success','own kernel stack entry','post-exit serial BOOT_OK'],
 'not_tested':['Windows apps/drivers','physical hardware','Secure Boot','interrupts','own page tables']}
(root/'build/boot-test.json').write_text(json.dumps(result,indent=2)+'\n')
print(output);print(json.dumps(result,indent=2))
sys.exit(0 if success else 1)
