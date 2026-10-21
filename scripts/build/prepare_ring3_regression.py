"""Reuse the actual P4 QEMU suite, adding one Ring 3 test in preview only."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
original=ROOT/"tests"/"runners"/"test_console_wrapper_fls_candidate.py"
result=ROOT/"tests"/"runners"/"test_ring3_preview.py"
text=original.read_text(encoding="utf-8")
anchor="            send('page-test','PAGING_TEST_OK')"
assert text.count(anchor)==1
check=(
 "            ring3_output=send('ring3-test','RING3_SMOKE_OK')\n"
 "            assert 'RING3_USER_WRITE_OK' in ring3_output\n"
 "            assert 'RING3_TEST_OK: CPL3 syscall write/exit, supervisor page access denied, kernel survived' in ring3_output\n"
 "            checks.append('Ring 3 privilege boundary: user int80 write/exit and recovered supervisor page fault')\n"
)
text=text.replace(anchor,anchor+"\n"+check)
result.write_text(text,encoding="utf8")
print("RING3_RUNNER_READY",result)
