"""Observe NTDLL table parsing in this process; never call ResetRtlTranslations."""
import ctypes as C
import hashlib
import json
import os
import struct
from pathlib import Path


def main():
    k = C.WinDLL('kernel32')
    n = C.WinDLL('ntdll')
    init = n.RtlInitNlsTables
    init.argtypes = [C.c_void_p] * 4
    init.restype = None
    system = Path(os.environ['SystemRoot']) / 'System32'
    paths = [system / f'c_{k.GetACP()}.nls', system / f'c_{k.GetOEMCP()}.nls', system / 'l_intl.nls']
    data = [p.read_bytes() for p in paths]
    owners = [C.create_string_buffer(d) for d in data]
    bases = [C.addressof(p) for p in owners]
    records = []
    for label, pointers in [('files', bases), ('null-inputs', [0, 0, 0])]:
        output = C.create_string_buffer(b'\xa5' * 0xb0)
        init(*pointers, C.addressof(output) + 16)
        raw = output.raw
        assert raw[:16] == b'\xa5' * 16 and raw[0xa0:0xb0] == b'\xa5' * 16
        body = raw[16:0xa0]
        row = {'case': label, 'canaries_intact': True, 'code_pages': [], 'case_offsets': []}
        for offset, source_index in [(0, 1), (0x40, 0)]:
            fields = struct.unpack_from('<7H', body, offset)
            actual = [struct.unpack_from('<Q', body, offset + x)[0] for x in [0x20, 0x28, 0x30, 0x38]]
            if label == 'null-inputs':
                assert fields == (65001, 4, 63, 65533, 63, 63, 0) and actual == [0] * 4
                relative = [None] * 4
            else:
                d, base = data[source_index], bases[source_index]
                word = lambda i: struct.unpack_from('<H', d, i * 2)[0]
                header = word(0)
                mb = (header + 1) * 2
                ranges = mb + 512 + 2 + (512 if struct.unpack_from('<H', d, mb + 512)[0] else 0)
                expected = [mb, (((word(header) + header) & 0xffff) + 1) * 2, ranges,
                            ranges + 2 if struct.unpack_from('<H', d, ranges)[0] else None]
                relative = [p - base if p else None for p in actual]
                assert relative == expected
                assert fields[0] == word(1)
                assert all(x is None or 0 <= x < len(d) for x in relative)
            row['code_pages'].append({'fields': list(fields), 'pointer_offsets': relative})
        for pos in [0x80, 0x88]:
            value = struct.unpack_from('<Q', body, pos)[0]
            row['case_offsets'].append(value - bases[2] if value else None)
        expected = [4, (((struct.unpack_from('<H', data[2], 2)[0] + 1) & 0xffff) + 1) * 2]
        assert row['case_offsets'] == (expected if label == 'files' else [None, None])
        records.append(row)
    result = {'passed': True, 'ntdll_sha256': hashlib.sha256((system/'ntdll.dll').read_bytes()).hexdigest(),
              'sources': [{'path': str(p), 'sha256': hashlib.sha256(d).hexdigest(), 'bytes': len(d)} for p, d in zip(paths, data)],
              'records': records, 'limits': 'Local parser only. No global translation reset, no conversion test, no guest execution; untouched structure padding is not compared.'}
    dest = Path(__file__).parent / 'build/nls-tables-windows.json'
    dest.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
