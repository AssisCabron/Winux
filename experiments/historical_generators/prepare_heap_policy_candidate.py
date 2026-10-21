"""Prepare an isolated Winux heap-policy integration candidate, without editing 0.38 sources."""
from pathlib import Path

root = Path(__file__).resolve().parent
src = root / "src"
header = (src / "nt_startup_test.h").read_text(encoding="utf-8")
needle = (
    " if(!free_block(private_heap,0,large_block))goto done;large_block=0;\n"
    " if(!destroy_heap(private_heap))goto done;private_heap=0;"
)
replacement = (
    " if(!free_block(private_heap,0,large_block))goto done;large_block=0;\n"
    " /* Isolated, version-pinned probe: original NTDLL heap-policy branch. */\n"
    " u32(*set_policy)(void*,u32,const u32*,u64)=(void*)module_export(n,\"RtlSetHeapInformation\",0,0);\n"
    " if(!set_policy)goto done;\n"
    " u32 policy=2;\n"
    " u32 short_status=set_policy(private_heap,0,&policy,0);\n"
    " policy=1;u32 bad_status=set_policy(private_heap,0,&policy,sizeof(policy));\n"
    " policy=2;u32 good_status=set_policy(private_heap,0,&policy,sizeof(policy));\n"
    " print(\"NT_HEAP_POLICY_PROBE statuses(short,bad,good): \");print_hex(short_status);\n"
    " print(\" \");print_hex(bad_status);print(\" \");print_hex(good_status);print(\"\\n\");\n"
    " if(short_status!=0xc0000023U||bad_status!=0xc0000001U||good_status)goto done;\n"
    " if(((u8*)private_heap)[0x1a3]!=2)goto done; /* Pinned NTDLL internal state. */\n"
    " if(set_policy(private_heap,0,&policy,sizeof(policy)))goto done;\n"
    " print(\"NT_HEAP_POLICY_OK: NTDLL policy 2, invalid paths, repeat and ownership\\n\");\n"
    " if(!destroy_heap(private_heap))goto done;private_heap=0;"
)
assert header.count(needle) == 1, "Cannot identify unique heap scope"
(src / "nt_startup_heap_policy_candidate.h").write_text(header.replace(needle, replacement), encoding="utf-8")
shell = (src / "shell.h").read_text(encoding="utf-8")
assert shell.count('#include "nt_startup_test.h"') == 1
(src / "shell_heap_policy_candidate.h").write_text(
    shell.replace('#include "nt_startup_test.h"', '#include "nt_startup_heap_policy_candidate.h"'),
    encoding="utf-8"
)
boot = (src / "boot.c").read_text(encoding="utf-8")
assert boot.count('#include "shell.h"') == 1
(src / "boot_heap_policy_candidate.c").write_text(
    boot.replace('#include "shell.h"', '#include "shell_heap_policy_candidate.h"')
        .replace("version 0.38", "version 0.38-P1-test")
        .replace("v0.38", "v0.38-P1-test"), encoding="utf-8"
)
iso_script = (root / "make_iso.py").read_text(encoding="utf-8")
for old, new in (
    ("BOOTX64.EFI", "BOOTX64-heap-policy.EFI"),
    ("winux-0.38-x64.iso", "winux-0.38-heap-policy-candidate.iso"),
    ("esp.img", "esp-heap-policy-candidate.img"),
    ("sha256.json", "sha256-heap-policy-candidate.json"),
):
    assert old in iso_script
    iso_script = iso_script.replace(old, new)
(root / "make_iso_heap_policy_candidate.py").write_text(iso_script, encoding="utf-8")
print("Candidate sources and isolated ISO packaging created; baseline unmodified.")
