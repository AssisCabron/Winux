"""Package user-editable, read-only RAM files and an original bitmap font."""
from pathlib import Path
import json, re, struct, argparse, hashlib

parser=argparse.ArgumentParser()
parser.add_argument('--cmd',type=Path)
parser.add_argument('--windows-dependencies',action='store_true')
args=parser.parse_args()

root = Path(__file__).resolve().parents[2]
build = root / 'build'
build.mkdir(exist_ok=True)
# Original 5x7 glyph drawings, including distinct lowercase shapes.
glyphs = {
'|':'00100 00100 00100 00100 00100 00100 00100',
'A':'01110 10001 10001 11111 10001 10001 10001',
'B':'11110 10001 10001 11110 10001 10001 11110',
'C':'01111 10000 10000 10000 10000 10000 01111',
'D':'11110 10001 10001 10001 10001 10001 11110',
'E':'11111 10000 10000 11110 10000 10000 11111',
'F':'11111 10000 10000 11110 10000 10000 10000',
'G':'01111 10000 10000 10111 10001 10001 01111',
'H':'10001 10001 10001 11111 10001 10001 10001',
'I':'11111 00100 00100 00100 00100 00100 11111',
'J':'00111 00010 00010 00010 10010 10010 01100',
'K':'10001 10010 10100 11000 10100 10010 10001',
'L':'10000 10000 10000 10000 10000 10000 11111',
'M':'10001 11011 10101 10101 10001 10001 10001',
'N':'10001 11001 11001 10101 10011 10011 10001',
'O':'01110 10001 10001 10001 10001 10001 01110',
'P':'11110 10001 10001 11110 10000 10000 10000',
'Q':'01110 10001 10001 10001 10101 10010 01101',
'R':'11110 10001 10001 11110 10100 10010 10001',
'S':'01111 10000 10000 01110 00001 00001 11110',
'T':'11111 00100 00100 00100 00100 00100 00100',
'U':'10001 10001 10001 10001 10001 10001 01110',
'V':'10001 10001 10001 10001 10001 01010 00100',
'W':'10001 10001 10001 10101 10101 11011 10001',
'X':'10001 10001 01010 00100 01010 10001 10001',
'Y':'10001 10001 01010 00100 00100 00100 00100',
'Z':'11111 00001 00010 00100 01000 10000 11111',
'a':'00000 00000 01110 00001 01111 10001 01111',
'b':'10000 10000 10110 11001 10001 10001 11110',
'c':'00000 00000 01111 10000 10000 10000 01111',
'd':'00001 00001 01101 10011 10001 10001 01111',
'e':'00000 00000 01110 10001 11111 10000 01110',
'f':'00110 01001 01000 11100 01000 01000 01000',
'g':'00000 01111 10001 10001 01111 00001 01110',
'h':'10000 10000 10110 11001 10001 10001 10001',
'i':'00100 00000 01100 00100 00100 00100 01110',
'j':'00010 00000 00110 00010 00010 10010 01100',
'k':'10000 10000 10010 10100 11000 10100 10010',
'l':'01100 00100 00100 00100 00100 00100 01110',
'm':'00000 00000 11010 10101 10101 10101 10101',
'n':'00000 00000 10110 11001 10001 10001 10001',
'o':'00000 00000 01110 10001 10001 10001 01110',
'p':'00000 11110 10001 10001 11110 10000 10000',
'q':'00000 01111 10001 10001 01111 00001 00001',
'r':'00000 00000 10110 11001 10000 10000 10000',
's':'00000 00000 01111 10000 01110 00001 11110',
't':'01000 01000 11100 01000 01000 01001 00110',
'u':'00000 00000 10001 10001 10001 10011 01101',
'v':'00000 00000 10001 10001 10001 01010 00100',
'w':'00000 00000 10001 10001 10101 10101 01010',
'x':'00000 00000 10001 01010 00100 01010 10001',
'y':'00000 10001 10001 10001 01111 00001 01110',
'z':'00000 00000 11111 00010 00100 01000 11111',
'0':'01110 10001 10011 10101 11001 10001 01110',
'1':'00100 01100 00100 00100 00100 00100 01110',
'2':'01110 10001 00001 00010 00100 01000 11111',
'3':'11110 00001 00001 01110 00001 00001 11110',
'4':'00010 00110 01010 10010 11111 00010 00010',
'5':'11111 10000 10000 11110 00001 00001 11110',
'6':'01110 10000 10000 11110 10001 10001 01110',
'7':'11111 00001 00010 00100 01000 01000 01000',
'8':'01110 10001 10001 01110 10001 10001 01110',
'9':'01110 10001 10001 01111 00001 00001 01110',
'.':'00000 00000 00000 00000 00000 00110 00110',
',':'00000 00000 00000 00000 00110 00110 00100',
':':'00000 00110 00110 00000 00110 00110 00000',
';':'00000 00110 00110 00000 00110 00100 01000',
'-':'00000 00000 00000 11111 00000 00000 00000',
'_':'00000 00000 00000 00000 00000 00000 11111',
'/':'00001 00010 00010 00100 01000 01000 10000',
'\\':'10000 01000 01000 00100 00010 00010 00001',
'>':'10000 01000 00100 00010 00100 01000 10000',
'<':'00001 00010 00100 01000 00100 00010 00001',
'=':'00000 00000 11111 00000 11111 00000 00000',
'[':'01110 01000 01000 01000 01000 01000 01110',
']':'01110 00010 00010 00010 00010 00010 01110',
'(':'00010 00100 01000 01000 01000 00100 00010',
')':'01000 00100 00010 00010 00010 00100 01000',
'!':'00100 00100 00100 00100 00100 00000 00100',
'?':'01110 10001 00001 00010 00100 00000 00100',
'+':'00000 00100 00100 11111 00100 00100 00000',
'#':'01010 01010 11111 01010 11111 01010 01010',
'%':'11001 11010 00100 00100 01000 10110 00110',
'*':'00000 10101 01110 11111 01110 10101 00000',
'"':'01010 01010 00000 00000 00000 00000 00000',
"'":'00100 00100 00000 00000 00000 00000 00000',
' ':'00000 00000 00000 00000 00000 00000 00000',
}
font = []
for i in range(128):
    shape = glyphs.get(chr(i), glyphs['?'])
    font.append('{' + ','.join(str(int(row, 2)) for row in shape.split()) + '}')
(build/'font.h').write_text('static const u8 font[128][7] = {\n' + ',\n'.join(font) + '\n};\n')
fixture = bytearray(1024)
fixture[:2] = b'MZ'
struct.pack_into('<I', fixture, 0x3c, 0x80)
fixture[0x80:0x84] = b'PE\0\0'
struct.pack_into('<HHIIIHH', fixture, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
o=0x98
struct.pack_into('<H', fixture,o,0x20b)
struct.pack_into('<I', fixture,o+16,0x1000)
struct.pack_into('<Q', fixture,o+24,0x140000000)
struct.pack_into('<II', fixture,o+32,4096,512)
struct.pack_into('<II', fixture,o+56,8192,512)
struct.pack_into('<H', fixture,o+68,3)
struct.pack_into('<I', fixture,o+108,16)
section=o+240
fixture[section:section+8]=b'.text\0\0\0'
struct.pack_into('<IIII',fixture,section+8,6,4096,512,512)
struct.pack_into('<I',fixture,section+36,0x60000020)
fixture[512:518]=bytes.fromhex('b8 2a 00 00 00 c3')
(build/'sample.pe').write_bytes(fixture)
files = sorted((root/'ramfs').glob('*')) + [build/'sample.pe',build/'hello.exe',build/'runtime.exe',build/'dlltest.exe',build/'dllexit.exe',build/'tlstest.exe',build/'ntmemory.exe',build/'winuxrt.dll',build/'winuxbridge.dll']
if args.cmd:
    if args.cmd.name.lower()!='cmd.exe':raise ValueError('Expected cmd.exe')
    files.append(args.cmd)
from prepare_windows import prepare
if args.windows_dependencies and not args.cmd:raise ValueError('Windows dependencies require the inventoried CMD')
files += prepare(args.windows_dependencies)
if len({p.name.lower() for p in files})!=len(files):raise ValueError('Duplicate RAM file names')
if len(files) > 32:
    raise ValueError('RAM filesystem supports at most 32 files')
source = ['typedef struct {const char *name; const u8 *data; u64 size;} ram_file;']
entries = []
total = 0
manifest=[]
for i, file in enumerate(files):
    if not file.is_file() or not re.fullmatch(r'[a-zA-Z0-9_.-]{1,48}', file.name):
        raise ValueError('Only flat files with simple ASCII names are supported')
    data = file.read_bytes()
    total += len(data)
    manifest.append({'name':file.name,'source':str(file.resolve()),'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    if total > 16*1024*1024:
        raise ValueError('RAM filesystem exceeds 16 MiB')
    source.append(f'static const u8 ram_data_{i}[] = {{' + ','.join(str(b) for b in data) + ',0};' if data else f'static const u8 ram_data_{i}[] = {{0}};')
    entries.append('{' + json.dumps(file.name) + f',ram_data_{i},{len(data)}' + '}')
source.append('static const ram_file ram_files[] = {' + ','.join(entries) + '};')
source.append(f'#define RAM_FILE_COUNT {len(entries)}')
(build/'ramfs.h').write_text('\n'.join(source))
(build/'ramfs-manifest.json').write_text(json.dumps(manifest,indent=2))
# Each CPU exception has its own vector; fatal path never returns to damaged context.
stubs = []
errors = {8,10,11,12,13,14,17,21,29,30}
for i in range(32):
    err = 'movq (%rsp), %rdx' if i in errors else 'xorq %rdx, %rdx'
    stubs.append(f'__attribute__((naked,noreturn)) static void exception_{i}(void) {{ __asm__ volatile("cli; cld; {err}; movq ${i}, %rcx; andq $-16, %rsp; subq $32, %rsp; call kernel_fault; 1: hlt; jmp 1b"); }}')
stubs.append('static void (*exception_handlers[32])(void) = {' + ','.join(f'exception_{i}' for i in range(32)) + '};')
(build/'exceptions.h').write_text('\n'.join(stubs))
print(f'Packaged {len(entries)} files, {total} bytes')
