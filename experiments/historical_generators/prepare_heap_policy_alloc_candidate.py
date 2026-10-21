"""Second isolated Winux image: exercise NTDLL heap allocations after policy 2."""
from pathlib import Path
root=Path(__file__).resolve().parent
src=root/"src"
header=(src/"nt_startup_heap_policy_candidate.h").read_text(encoding="utf-8")
needle=' print("NT_HEAP_POLICY_OK: NTDLL policy 2, invalid paths, repeat and ownership\\n");\n'
assert header.count(needle)==1, "Missing validated policy marker"
addition=(
    ' const u32 policy_sizes[]={8,16,32,64,128,512,4096,65536};\n'
    ' for(unsigned slot=0;slot<sizeof(policy_sizes)/sizeof(policy_sizes[0]);slot++){\n'
    '  u32 amount=policy_sizes[slot];\n'
    '  void *block=allocate(private_heap,8,amount);\n'
    '  if(!block||!nt_obj_readable(block,amount)){print("NT_LFH_ALLOCATION_FAILED: inaccessible\\n");goto done;}\n'
    '  for(u32 byte=0;byte<amount;byte++)if(((u8*)block)[byte]){print("NT_LFH_ALLOCATION_FAILED: nonzero\\n");goto done;}\n'
    '  ((u8*)block)[amount-1]=0x5a;\n'
    '  if(((u8*)block)[amount-1]!=0x5a||!free_block(private_heap,0,block)){print("NT_LFH_ALLOCATION_FAILED: free\\n");goto done;}\n'
    ' }\n'
    ' print("NT_LFH_ALLOCATION_OK: 8 sizes, zero, write, free after policy2\\n");\n'
)
(src/"nt_startup_heap_policy_alloc_candidate.h").write_text(header.replace(needle,needle+addition),encoding="utf-8")
shell=(src/"shell_heap_policy_candidate.h").read_text(encoding="utf-8")
old='#include "nt_startup_heap_policy_candidate.h"'
assert shell.count(old)==1
(src/"shell_heap_policy_alloc_candidate.h").write_text(shell.replace(old,'#include "nt_startup_heap_policy_alloc_candidate.h"'),encoding="utf-8")
boot=(src/"boot_heap_policy_candidate.c").read_text(encoding="utf-8")
old='#include "shell_heap_policy_candidate.h"'
assert boot.count(old)==1
(src/"boot_heap_policy_alloc_candidate.c").write_text(boot.replace(old,'#include "shell_heap_policy_alloc_candidate.h"').replace("0.38-P1-test","0.38-P2-test"),encoding="utf-8")
iso=(root/"make_iso_heap_policy_candidate.py").read_text(encoding="utf-8")
assert "BOOTX64-heap-policy.EFI" in iso
(root/"make_iso_heap_policy_alloc_candidate.py").write_text(iso.replace("heap-policy", "heap-policy-alloc"),encoding="utf-8")
suite=(root/"test_console_heap_policy_candidate.py").read_text(encoding="utf-8")
assert "winux-0.38-heap-policy-candidate.iso" in suite
assert "console-heap-candidate" in suite
suite=suite.replace("winux-0.38-heap-policy-candidate.iso","winux-0.38-heap-policy-alloc-candidate.iso")
suite=suite.replace("console-heap-candidate","console-heap-alloc-candidate")
old="                    assert 'NT_HEAP_POLICY_OK:' in startup_text, startup_text[-2500:]"
assert suite.count(old)==1
suite=suite.replace(old,old+"\n                    assert 'NT_LFH_ALLOCATION_OK:' in startup_text, startup_text[-2500:]")
(root/"test_console_heap_policy_alloc_candidate.py").write_text(suite,encoding="utf-8")
print("P2 isolated allocation test and ISO packer ready, P1 and baseline unchanged.")
