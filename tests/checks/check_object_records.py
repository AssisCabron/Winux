"""Compare guest original-stub records to host observations; shared by VM tests."""
import json


def check_object_records(text, build):
    event = json.loads((build / "nt-events-windows.json").read_text())["records"]
    namespace = json.loads((build / "nt-namespace-windows.json").read_text())["records"]
    expected = []
    for r in event:
        v = [int(r["status"], 16)]
        if "previous" in r:
            v.append(r["previous"] & 0xffffffff)
        expected.append("NT_OBJECT event " + ",".join(map(str, v)))
    for r in namespace:
        v = [int(r["status"], 16)]
        if r["operation"] == "query":
            v += [r["length"], r["required"], int(r["value"] == "C:\\Windows\\System32")]
        expected.append("NT_OBJECT namespace " + ",".join(map(str, v)))
    actual = [line.strip() for line in text.splitlines() if line.startswith("NT_OBJECT ")]
    assert len(expected) == 47 and actual == expected, (actual, expected)
