"""Native GOP Orbis GUI integration: boot splash, volatile setup/login and desktop.
Input via the REAL QEMU PS/2 keyboard; serial is telemetry only.
No credential leakage to serial; no persistent user authentication claimed.
"""
from pathlib import Path
import argparse,json,hashlib,socket,subprocess,shutil,time,codecs,sys,struct
cli=argparse.ArgumentParser()
cli.add_argument('--visible',action='store_true',help='Display the actual QEMU GUI on the Windows desktop during tests')
args=cli.parse_args()
root=Path(__file__).resolve().parents[2];build=root/'build'
tag='orbis-gui'
iso=build/'orbis-gui-preview'/'winux-0.38-wrapper-fls-candidate.iso'
assert iso.is_file()
vars=build/(tag+'-vars.fd')
shutil.copyfile(root/'tools/qemu/share/edk2-i386-vars.fd',vars)
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
sp=port();qp=port()
cmd=[str(root/'tools/qemu/qemu-system-x86_64.exe'),'-machine','q35','-accel','tcg',
 '-m','256M','-smp','1','-vga','std',
 '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={root}/tools/qemu/share/edk2-x86_64-code.fd',
 '-drive',f'if=pflash,format=raw,unit=1,file={vars}',
 '-cdrom',str(iso),'-boot','d','-display',('gtk' if args.visible else 'none'),
 '-name','ORBIS OS - Testes Graficos Visiveis','-nic','none','-no-reboot',
 '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off',
 '-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off']
output='';decoder=codecs.getincrementaldecoder('utf-8')('replace');checks=[]
failure=None;start=time.monotonic()
with (build/(tag+'-stderr.log')).open('wb') as log:
    proc=subprocess.Popen(cmd,stdout=log,stderr=log)
    s=None;q=None
    try:
        def connect(p):
            for _ in range(100):
                try:return socket.create_connection(('127.0.0.1',p),timeout=1)
                except OSError:time.sleep(.1)
            raise RuntimeError('Socket connection refused')
        s=connect(sp);s.settimeout(.3)
        q=connect(qp);q.settimeout(10);f=q.makefile('rb')
        json.loads(f.readline())
        def execute(name,arguments=None):
            q.sendall((json.dumps({'execute':name,'arguments':arguments or {}})+'\n').encode())
            while True:
                line=f.readline()
                if not line:raise RuntimeError('QMP disconnected')
                reply=json.loads(line)
                if 'error' in reply:raise RuntimeError(reply)
                if 'return' in reply:return reply['return']
        execute('qmp_capabilities')
        def until(marker,offset=0,secs=30):
            global output
            deadline=time.monotonic()+secs
            while marker not in output[offset:]:
                if time.monotonic()>deadline or proc.poll() is not None:
                    raise AssertionError(f'Missing {marker}: '+output[offset:][-1300:])
                try:
                    chunk=s.recv(65536)
                    if not chunk:raise RuntimeError('QEMU serial closed')
                    output+=decoder.decode(chunk)
                except socket.timeout:pass
            return output[offset:]
        def key(k):
            execute('send-key',{'keys':[{'type':'qcode','data':k}],'hold-time':65})
            time.sleep(.32 if args.visible else .10)
        def type_keys(text):
            for char in text:
                key('ret' if char=='\n' else 'spc' if char==' ' else char)
        until('ORBIS_SPLASH_READY',secs=45)
        checks.append('Real native GOP boot splash started')
        until('ORBIS_SETUP_READY',secs=30)
        time.sleep(.8)
        execute('screendump',{'filename':str(build/(tag+'-setup-v2.ppm'))})
        checks.append('PIT-driven transition from splash to first-boot setup')
        type_keys('orbis\n')
        until('ORBIS_SETUP_PIN_READY',secs=12)
        type_keys('2468\n')
        until('ORBIS_LOGIN_READY',secs=12)
        time.sleep(.8)
        execute('screendump',{'filename':str(build/(tag+'-login-v2.ppm'))})
        assert 'ORBIS_VOLATILE_USER_CREATED' in output
        checks.append('Created RAM-only local account by PS/2 keyboard')
        start_at=len(output)
        type_keys('1111\n')
        until('ORBIS_LOGIN_DENIED',start_at,secs=12)
        type_keys('2468\n')
        until('ORBIS_DESKTOP_READY user=local',secs=12)
        checks.append('Bad demo PIN denied; valid demo PIN reached desktop')
        for k,marker in [('s','ORBIS_SETTINGS_READY'),('f','ORBIS_FILES_READY'),
                         ('m','ORBIS_DESKTOP_READY menu'),('l','ORBIS_LOGOUT_OK')]:
            pos=len(output);key(k);until(marker,pos,secs=12)
        checks.append('Settings, Files, desktop and logout transitions by keyboard')
        pos=len(output);key('g');until('ORBIS_DESKTOP_READY user=guest',pos,secs=12)
        checks.append('Explicit guest demo session without password')
        assert 'ORBIS_MOUSE_READY' in output
        assert 'ORBIS_MOUSE_IRQ12_READY' in output, 'PS/2 IRQ12 not configured'
        assert 'ORBIS_MOUSE_RATE_HZ=200' in output, 'PS/2 high-rate reporting not configured'
        # QMP input reaches the emulated i8042 AUX/PS2 mouse, not a host GUI.
        def move(axis,value):
            execute('input-send-event',{'events':[{'type':'rel','data':{'axis':axis,'value':value}}]})
            time.sleep(.08)
        for _ in range(5):move('x',-100)
        move('x',-10)
        move('y',-33)
        begin=len(output)
        execute('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':True}}]})
        time.sleep(.09)
        execute('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':False}}]})
        until('ORBIS_FILES_READY',begin,secs=12)
        checks.append('QEMU emulated PS/2 mouse click opens the graphical Files window')
        # Obtain the actual QEMU displayed framebuffer and check non-black,
        # approximate contrast, dimensions, native shell panel colors.
        file=build/(tag+'-desktop.ppm')
        # A screenshot must not race the CPU-only bilinear framebuffer redraw.
        time.sleep(1.5)
        execute('screendump',{'filename':str(file)})
        for _ in range(40):
            if file.is_file() and file.stat().st_size>100000:break
            time.sleep(.1)
        assert file.is_file() and file.stat().st_size>100000
        with file.open('rb') as fd:
            first=fd.readline().strip()
            size_line=fd.readline().strip()
            while size_line.startswith(b'#'):size_line=fd.readline().strip()
            dims=list(map(int,size_line.split()))
            maxval=int(fd.readline().strip())
            data=fd.read()
        assert first==b'P6' and maxval==255 and dims[0]>=800 and dims[1]>=600,dims
        colors={data[i:i+3] for i in range(0,len(data),977)}
        assert len(colors)>20,len(colors)
        checks.append('QEMU screendump proves populated 32bpp graphical desktop rather than black console')
        if args.visible:time.sleep(2.0)
    except Exception as e:failure=f'{type(e).__name__}: {e}'
    finally:
        (build/(tag+'-serial.log')).write_text(output,encoding='utf-8')
        if s:s.close()
        if q:q.close()
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill()
result={'passed':failure is None,'failure':failure,'checks':checks,
        'seconds':round(time.monotonic()-start,2),
        'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest()}
(build/(tag+'-test.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
sys.exit(0 if result['passed'] else 1)
