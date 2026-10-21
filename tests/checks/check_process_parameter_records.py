"""Compare original guest Create/Normalize/Destroy results with retained host fixtures."""
import json


def check_process_parameter_records(text, build):
    oracle = json.loads((build / 'process-parameters-windows.json').read_text())
    assert oracle['passed']
    assert oracle['ntdll_sha256'] == '36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d'
    expected = []

    def record(label, values):
        expected.append('NT_PROCESS_PARAMS ' + label + ' ' + ','.join(map(str, values)))

    for row in oracle['records']:
        flags = row['input_flags']
        record('header', [flags, row['maximum'], row['length'], int(row['normalized']),
                          int(row['normalization_idempotent'])])
        for s in row['strings']:
            record('string', [flags, int(s['offset'], 16), s['length'], s['capacity'],
                              s['buffer_offset'] or 0])
        record('environment', [flags, int(row['explicit_environment_preserved']), row['environment_size']])
    for row in oracle['negative']:
        record(row['case'], [row['status'], int(row['output_null'])])
    actual = [line.strip() for line in text.splitlines() if line.startswith('NT_PROCESS_PARAMS ')]
    assert len(expected) == 23 and actual == expected, (actual, expected)
    assert 'NT_PROCESS_PARAMS_OK' in text
