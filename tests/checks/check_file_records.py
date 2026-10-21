"""Compare real guest SYSCALL I/O with complete Windows buffers and IOSBs."""
import json


def check_file_records(text, build):
    oracle=json.loads((build/'file-io-windows.json').read_text())
    assert oracle['ntdll_sha256']=='36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    expected=['NT_FILE '+r['step']+' '+str(r['status'])+' '+r['iosb']+' '+r['buffer'] for r in oracle['records']]
    actual=[s.strip() for s in text.splitlines() if s.startswith('NT_FILE ')]
    assert len(expected)==14 and actual==expected,(actual,expected)
    assert 'NT_FILE_WRAPPERS_OK' in text and 'NT_FILE_TEST_OK' in text
