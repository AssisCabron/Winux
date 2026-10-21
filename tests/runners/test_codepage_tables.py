import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Check version-bound NLS node layout against measured Windows conversions."""
import hashlib
import json
import struct
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2]
    fixture = json.loads((root / 'build/codepages-windows.json').read_text())
    source = Path(fixture['source'])
    assert hashlib.sha256(source.read_bytes()).hexdigest() == fixture['sha256']
    tables = {}
    provenance = []
    for cp in (1252, 850):
        path = Path(f'C:/Windows/System32/c_{cp}.nls')
        data = path.read_bytes()
        word = lambda off: struct.unpack_from('<H', data, off)[0]
        header = word(0)
        width = word(4)
        assert word(2) == cp and width == 1
        # RVA 4acb4: node+38 points after the first two size words/sections.
        wide_offset = 2 * ((header + word(2 * header)) & 0xffff) + 2
        assert wide_offset + 65536 <= len(data)
        tables[cp] = (data, wide_offset)
        provenance.append(dict(cp=cp, sha256=hashlib.sha256(data).hexdigest(),
                               table_offset=wide_offset, bytes=len(data)))
    checked = 0
    for record in fixture['records']:
        if 'utf16' not in record:
            continue
        data, offset = tables[record['cp']]
        raw = bytes.fromhex(record['utf16'])
        units = struct.unpack('<'+'H'*(len(raw)//2), raw)
        actual = bytes(data[offset + unit] for unit in units)
        assert actual.hex() == record['output'], record
        assert len(actual) == record['size'], record
        checked += 1
    result = dict(scope='Offline SBCS table conversion only; not guest execution or full API',
                  conversions=checked, tables=provenance)
    path = root / 'build/codepage-tables-test.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
