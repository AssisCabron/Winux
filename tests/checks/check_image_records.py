"""Compare class 6 and original RtlPcToFileHeader to host observations."""
import json


def check_image_records(text, build):
    for name in ('image-information-windows.json', 'pc-to-file-header-windows.json'):
        assert json.loads((build/name).read_text())['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    expected = []
    for r in json.loads((build/'image-information-windows.json').read_text())['records']:
        values = [r['size'], r['status'], r['returned'], int(r['untouched'])]
        if r['status'] == 0:
            # High flag bits describe host image policy, not guest signature trust.
            values += [int(r['base_is_ntdll']), int(r['base_is_zero']), r['image_size'],
                       r['flags'] & 3, int(r['padding'] == '00000000')]
        expected.append('NT_IMAGE '+r['step']+' '+','.join(map(str, values)))
    actual = [s.strip() for s in text.splitlines() if s.startswith('NT_IMAGE ')]
    assert len(expected) == 16 and actual == expected, (actual, expected)
    expected = []
    for r in json.loads((build/'pc-to-file-header-windows.json').read_text())['records']:
        if 'module' in r:
            expected.append('NT_PC '+r['module']+' '+r['step']+' 1,1')
        else:
            expected.append('NT_PC '+r['step']+' '+str(r['result'])+','+str(r['output']))
    actual = [s.strip() for s in text.splitlines() if s.startswith('NT_PC ')]
    assert len(expected) == 11 and actual == expected, (actual, expected)
    assert 'NT_IMAGE_LIFECYCLE_OK' in text and 'NT_IMAGE_TEST_OK' in text
