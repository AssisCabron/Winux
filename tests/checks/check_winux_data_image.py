"""Read-only verifier of the Winux-tagged FAT16 data volume after UEFI installation."""
from pathlib import Path
import sys,struct,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
IMG=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"build"/"ring3-preview"/"winux-data.img"
blob=IMG.read_bytes()
assert blob[510:512]==b'\x55\xaa'
start=struct.unpack_from("<I",blob,446+8)[0]
count=struct.unpack_from("<I",blob,446+12)[0]
assert blob[446+4]==6 and start==2048 and count==61440
b=start*512
assert blob[b+510:b+512]==b'\x55\xaa'
bps=struct.unpack_from("<H",blob,b+11)[0]
spc=blob[b+13]
reserved=struct.unpack_from("<H",blob,b+14)[0]
fats=blob[b+16]
fatsectors=struct.unpack_from("<H",blob,b+22)[0]
roots=struct.unpack_from("<H",blob,b+17)[0]
assert bps==512 and spc==2 and reserved==1 and fats==2 and fatsectors==120 and roots==512
root_offset=(start+reserved+fats*fatsectors)*512
fat_offset=(start+reserved)*512
data_lba=start+reserved+fats*fatsectors+(roots*32+bps-1)//bps
def cluster_bytes(n):
    assert 2<=n<0xfff0
    return blob[(data_lba+(n-2)*spc)*bps:(data_lba+(n-1)*spc)*bps]
def scan(data):
    found={}
    for off in range(0,len(data),32):
        e=data[off:off+32]
        if not e or e[0]==0:break
        if e[0]==0xe5 or e[11]==0x0f:continue
        base=e[:8].decode('ascii','replace').rstrip()
        ext=e[8:11].decode('ascii','replace').rstrip()
        name=base+('.'+ext if ext else '')
        if name in {'.','..'}:continue
        found[name]={"attr":e[11],"cluster":struct.unpack_from("<H",e,26)[0],
                     "size":struct.unpack_from("<I",e,28)[0]}
    return found
def ls_dir(path=()):
    current=scan(blob[root_offset:root_offset+roots*32])
    for name in path:
        target=current[name]
        assert target["attr"] & 16, (path,name,target)
        c=target["cluster"]
        parts=[]
        for _ in range(64):
            parts.append(cluster_bytes(c))
            nextc=struct.unpack_from("<H",blob,fat_offset+c*2)[0]
            if nextc>=0xfff8:break
            assert 2<=nextc<0xfff0
            c=nextc
        current=scan(b''.join(parts))
    return current
root=ls_dir()
assert root["WINUX.ID"]["size"]==13 and cluster_bytes(root["WINUX.ID"]["cluster"])[:13]==b"WINUX-DATA-V1"
expected_root=["SYSTEM","USERS","APPS","VAR","TMP"]
for item in expected_root:assert item in root and root[item]["attr"]&16,(item,root)
system=ls_dir(("SYSTEM",))
for item in ["CONFIG","DRIVERS","LIBS"]:assert item in system and system[item]["attr"]&16,(item,system)
for item in ["WIN32","ELF"]:assert item in ls_dir(("SYSTEM","LIBS"))
for item in ["LOG","CACHE"]:assert item in ls_dir(("VAR",))
config=ls_dir(("SYSTEM","CONFIG"))
assert "SYSTEM.INI" in config and "USERS.INI" in config,config
def filedata(entry):
    c=entry["cluster"]; parts=[]
    for _ in range(64):
        parts.append(cluster_bytes(c))
        if sum(len(part) for part in parts)>=entry["size"]:break
        nxt=struct.unpack_from("<H",blob,fat_offset+c*2)[0]
        assert 2<=nxt<0xfff0;c=nxt
    return b''.join(parts)[:entry["size"]]
system_cfg=filedata(config["SYSTEM.INI"])
users_cfg=filedata(config["USERS.INI"])
assert b"setup=required" in system_cfg and b"accounts=unprovisioned" in system_cfg
assert b"count=0" in users_cfg and b"setup=required" in users_cfg
out={"passed":True,"image_sha256":hashlib.sha256(blob).hexdigest(),
     "root_entries":sorted(root),"system_entries":sorted(system),"config_entries":sorted(config),
     "system_config":system_cfg.decode(),"users_config":users_cfg.decode()}
print(json.dumps(out,indent=2))
