"""Clone existing P4 QEMU regression against separately repacked image."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
src=root/"tests"/"runners"/"test_console_wrapper_fls_candidate.py"
dst=root/"tests"/"runners"/"test_console_repacked_p4.py"
text=src.read_text(encoding="utf-8")
old="iso=build/'winux-0.38-wrapper-fls-candidate.iso'"
assert text.count(old)==1
text=text.replace(old,"iso=build/'layout-verification'/'winux-0.38-wrapper-fls-candidate.iso'")
assert text.count("console-wrapper-fls-candidate")>0
text=text.replace("console-wrapper-fls-candidate","console-repacked-p4")
assert "'NT_HEAP_POLICY_OK:'" in text and "KB_FLS_TEARDOWN_OK" in text
dst.write_text(text,encoding="utf-8")
print(f"CLONED_QEMU_REGRESSION {dst}")
