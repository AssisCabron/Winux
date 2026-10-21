"""Read-only NT process-cookie/basic-system-information observations on this process."""
from pathlib import Path
import ctypes as c,hashlib,json,os
root=Path(__file__).resolve().parents[2];nt=c.WinDLL('ntdll');P=c.c_void_p;U=c.c_uint32
process=nt.NtQueryInformationProcess;process.restype=c.c_int32;process.argtypes=[P,U,P,U,c.POINTER(U)]
system=nt.NtQuerySystemInformation;system.restype=c.c_int32;system.argtypes=[U,P,U,c.POINTER(U)]
class BASIC(c.Structure):
    _fields_=[('reserved',U),('timer',U),('page_size',U),('physical_pages',U),('lowest_page',U),('highest_page',U),
              ('granularity',U),('minimum',c.c_uint64),('maximum',c.c_uint64),('affinity',c.c_uint64),('processors',c.c_ubyte)]
assert c.sizeof(BASIC)==64 and BASIC.maximum.offset==40
lines=[];records=[];cookie=None;basic=None
for kind,lengths in [('process',[0,3,4,8]),('system',[0,63,64,72])]:
    for length in lengths:
        buf=(c.c_ubyte*80)(*[0xa5]*80);returned=U(0x1234)
        s=(process(P(-1),0x24,buf,length,c.byref(returned)) if kind=='process' else system(0,buf,length,c.byref(returned)))&0xffffffff
        values=[s,returned.value]
        if s==0:
            if kind=='process':
                value=U.from_buffer(buf).value
                if cookie is None:cookie=value
                values.extend([int(value==cookie),int(bytes(buf)[4:]==bytes([0xa5])*76)])
            else:
                basic=BASIC.from_buffer_copy(buf)
                values.extend([basic.page_size,basic.granularity,int(basic.minimum<basic.maximum),int(basic.physical_pages>0),int(basic.processors>0),int(bytes(buf)[64:]==bytes([0xa5])*16)])
        else:values.append(int(bytes(buf)==bytes([0xa5])*80))
        line='NT_INFO '+kind+'-'+str(length)+' '+','.join(map(str,values));lines.append(line)
        records.append({'kind':kind,'length':length,'values':values})
assert cookie is not None and basic is not None
first=U();second=U();assert process(P(-1),0x24,c.byref(first),4,None)==0 and process(P(-1),0x24,c.byref(second),4,None)==0
assert first.value==second.value==cookie
lines.append('NT_INFO cookie-stable 1')
report={'passed':True,'lines':lines,'records':records,'system':{name:getattr(basic,name) for name,_ in BASIC._fields_},
        'cookie_value_omitted':True,'ntdll_sha256':hashlib.sha256((Path(os.environ['SystemRoot'])/'System32/ntdll.dll').read_bytes()).hexdigest(),
        'scope':'Exact sizes/status and normalized field invariants only; host CPU/RAM/address values are not expected in the guest.'}
(root/'build/windows-nt-info-probe.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
