"""Compare owned-slot FLS operations; thread teardown is a separate guest check."""
import json


def check_fls_records(text, build):
    oracle = json.loads((build / 'rtl-fls-windows.json').read_text())
    assert oracle['completed']
    assert oracle['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    expected = ['NT_FLS ' + r['step'] + ' ' + ','.join(str(int(v)) for v in r['values'])
                for r in oracle['records']]
    actual = [line.strip() for line in text.splitlines() if line.startswith('NT_FLS ')]
    assert len(expected) == 11 and actual == expected, (actual, expected)
    assert 'NT_FLS_TEARDOWN_OK' in text and 'NT_FLS_TEST_OK' in text
