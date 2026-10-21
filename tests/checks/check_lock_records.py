"""Compare uncontended guest locks with host records; normalize only CPU spin."""
import json


def check_lock_records(text, build):
    oracle = json.loads((build/'critical-section-windows.json').read_text())
    assert oracle['passed']
    expected = []
    for r in oracle['records']:
        values = [r['flags'], r['status']]
        if r['step'] != 'delete':
            values += [r['lock_count'] & 0xffffffff, r['recursion'], int(r['owner_matches']),
                       int(r['debug_info_kind'] == 'present'), int(r['event_created']), 0]
        expected.append('NT_LOCK '+r['step']+' '+','.join(map(str, values)))
    for r in oracle['invalid']:
        expected.append('NT_LOCK invalid '+','.join(map(str,[r['spin'],r['flags'],r['status'],int(r['unchanged'])])))
    actual = [line.strip() for line in text.splitlines() if line.startswith('NT_LOCK ')]
    assert len(expected) == 15 and actual == expected, (actual, expected)
    assert 'NT_LOCK_POOL_OK: eight live debug nodes, reverse deletion, pool restored' in text
    assert 'NT_LOCK_TEST_OK' in text
