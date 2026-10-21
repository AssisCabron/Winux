"""Compare all emitted NLS records with the measured host API results."""
import json


def check_codepage_records(text, build):
    oracle = json.loads((build / 'codepages-windows.json').read_text())
    assert oracle['sha256'] == 'b4ad8c8e3632104f4d2132ecb8979b8df279f0046dc42e79bed5acb744751871'
    expected = []
    for r in oracle['records']:
        if r['step'] == 'cpinfo':
            values = [r['cp'], int(r['valid']), r['max_char_size'], *r['default'], *r['lead']]
        elif r['step'].endswith('-short'):
            values = [r['cp'], r['result'], r['error']]
        else:
            values = [r['cp'], r['size'], *bytes.fromhex(r['output'])]
        expected.append('KB_CP '+r['step']+' '+','.join(map(str, values)))
    actual = [s.strip() for s in text.splitlines() if s.startswith('KB_CP ')]
    assert len(expected) == 20 and actual == expected, (actual, expected)
    assert 'KB_CP_TEARDOWN_OK' in text and 'KB_CP_TEST_OK' in text
