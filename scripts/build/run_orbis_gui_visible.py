"""Launch the experimental ORBIS OS ISO in a persistent visible GTK QEMU window.
Deliberately does not automate input or close the window.
"""
from pathlib import Path
import json,subprocess,shutil,hashlib,sys,time
root=Path(__file__).resolve().parents[2]
build=root/'build'
iso=build/'orbis-gui-preview'/'winux-0.38-wrapper-fls-candidate.iso'
if not iso.is_file():raise SystemExit("Build first: python scripts/build/make_proc_space_preview.py --experiment integration --orbis-gui")
sha=hashlib.sha256(iso.read_bytes()).hexdigest()
runtime_iso=iso.parent/('orbis-visible-'+sha[:12]+'.iso')
if not runtime_iso.exists():shutil.copyfile(iso,runtime_iso)
state=build/'orbis-gui-preview'/('visible-uefi-vars-'+sha[:12]+'.fd')
template=root/'tools/qemu/share/edk2-i386-vars.fd'
# Preserve an already booted guest's firmware variables; only create once.
if not state.exists():shutil.copyfile(template,state)
qemu=root/'tools/qemu/qemu-system-x86_64.exe'
command=[str(qemu),'-machine','q35','-accel','tcg','-m','256M','-smp','1','-vga','std',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={state}',
 '-cdrom',str(runtime_iso),'-boot','d','-display','gtk',
 '-name','ORBIS OS - RGB888 / Fonte Proporcional / Mouse IRQ12',
 '-nic','none','-no-reboot','-serial',f'file:{build}/orbis-gui-visible-serial.log']
p=subprocess.Popen(command,creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
time.sleep(1.5)
if p.poll() is not None:raise SystemExit('QEMU exited early: '+str(p.returncode))
manifest={'qemu_pid':p.pid,'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest(),
          'mode':'visible-gtk','manual_interaction':True,
          'serial_log':str(build/'orbis-gui-visible-serial.log')}
(build/'orbis-gui-visible.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
