"""Compare the selected growable Microsoft heap operations, not internal policy."""
import json


def check_heap_records(text, build):
    oracle = json.loads((build / 'rtl-heap-windows.json').read_text())
    assert oracle['completed'] and oracle['reserve_bytes'] == 0x100000
    assert oracle['commit_bytes'] == 0x1000
    assert oracle['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    expected = []
    for row in oracle['records']:
        if row['flags'] != 2:
            continue
        operation = row['operation']
        if operation == 'allocate':
            values = [row['size'], int(row['aligned16']), int(row['zeroed']), row['reported_size']]
        elif operation == 'free':
            values = [row['size'], int(row['success'])]
        elif operation == 'reallocate':
            values = [int(row['preserved']), int(row['zero_tail']), row['reported_size']]
        else:
            values = [int(row['success'])]
        expected.append('NT_HEAP_BATCH ' + operation + ' ' + ','.join(map(str, values)))
    actual = [line.strip() for line in text.splitlines() if line.startswith('NT_HEAP_BATCH ')]
    assert len(expected) == 19 and actual == expected, (actual, expected)
