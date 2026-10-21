"""Clone the QEMU 0.38 regression for candidate, with independent output paths."""
from pathlib import Path

root = Path(__file__).resolve().parent
p = root / "test_console.py"
text = p.read_text(encoding="utf-8")
old_iso = "iso=build/'winux-0.38-x64.iso'"
assert text.count(old_iso) == 1
text = text.replace(old_iso, "iso=build/'winux-0.38-heap-policy-candidate.iso'")
old_tag = "('fault' if args.fault else 'console')"
assert text.count(old_tag) == 1
text = text.replace(old_tag, "('fault' if args.fault else 'console-heap-candidate')")
text = text.replace("build/'console.ppm'", "build/'console-heap-candidate.ppm'")
text = text.replace("build/'console.png'", "build/'console-heap-candidate.png'")
text = text.replace("            from PIL import Image\n            Image.open(build/'console-heap-candidate.ppm').save(build/'console-heap-candidate.png')", "            ppm=(build/'console-heap-candidate.ppm').read_bytes()\n            assert ppm.startswith(b'P6') and len(ppm)>100000")
old = "                    startup_text=send('nt-startup-test','NT_STARTUP_TEST_OK')"
assert text.count(old) == 1
text = text.replace(old, old + "\n                    assert 'NT_HEAP_POLICY_OK:' in startup_text, startup_text[-2500:]")
(root / "test_console_heap_policy_candidate.py").write_text(text, encoding="utf-8")
print("Isolated candidate QEMU suite generated (original tester unchanged).")
