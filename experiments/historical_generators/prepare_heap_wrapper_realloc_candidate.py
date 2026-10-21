"""Generate P3 candidate from P2 without modifying stable or prior candidate sources."""
from pathlib import Path
root=Path(__file__).resolve().parent
src=root/"src"
old_header=(src/"nt_startup_heap_policy_alloc_candidate.h").read_text(encoding="utf-8")
anchor=' u32 policy=2;\n'
before=(
 ' int(*kb_set_policy)(void*,u32,const u32*,u64)=(void*)module_export(k,"HeapSetInformation",0,0);\n'
 ' void*(*realloc_original)(void*,u32,void*,u64)=(void*)module_export(n,"RtlReAllocateHeap",0,0);\n'
 ' u64(*size_original)(void*,u32,void*)=(void*)module_export(n,"RtlSizeHeap",0,0);\n'
 ' if(!kb_set_policy||!realloc_original||!size_original)goto done;\n'
 ' /* KernelBase error conversion uses the pinned NTDLL and the guest TEB. */\n'
 ' u32 bad_length_error=0,bad_value_error=0;\n'
 ' last_error=0;u32 kb_short=(u32)kb_set_policy(private_heap,0,&(u32){2},0);bad_length_error=last_error;\n'
 ' last_error=0;u32 kb_bad=(u32)kb_set_policy(private_heap,0,&(u32){1},4);bad_value_error=last_error;\n'
 ' print("KB_HEAP_SET_FAILURES: ");number(kb_short);print(",");number(bad_length_error);\n'
 ' print(",");number(kb_bad);print(",");number(bad_value_error);print("\\n");\n'
 ' if(kb_short||kb_bad||bad_length_error!=122||bad_value_error!=31)goto done;\n'
)
assert old_header.count(anchor)==1
header=old_header.replace(anchor,before+anchor)
old=' policy=2;u32 good_status=set_policy(private_heap,0,&policy,sizeof(policy));\n'
new=' policy=2;u32 kb_success=(u32)kb_set_policy(private_heap,0,&policy,sizeof(policy));\n u32 good_status=kb_success?0:0xc0000001U;\n'
assert header.count(old)==1
header=header.replace(old,new)
key=' print("NT_LFH_ALLOCATION_OK: 8 sizes, zero, write, free after policy2\\n");\n'
assert header.count(key)==1
addition=(
 ' /* Probe genuine NTDLL reallocation after KernelBase selected heap policy. */\n'
 ' const u32 pairs[][2]={{32,128},{128,2048},{4096,8192},{128,16}};\n'
 ' for(unsigned t=0;t<4;t++){\n'
 '  u32 from=pairs[t][0],to=pairs[t][1];\n'
 '  u8 *ptr=allocate(private_heap,8,from);\n'
 '  if(!ptr||!nt_obj_readable(ptr,from)){print("NT_REALLOC_FAILED: allocate\\n");goto done;}\n'
 '  for(u32 z=0;z<from;z++){if(ptr[z]){free_block(private_heap,0,ptr);goto done;}ptr[z]=(u8)((z*13+7)&255);}\n'
 '  u8 *expanded=realloc_original(private_heap,8,ptr,to);\n'
 '  if(!expanded){free_block(private_heap,0,ptr);print("NT_REALLOC_FAILED: reallocate\\n");goto done;}\n'
 '  int valid=nt_obj_readable(expanded,to)&&size_original(private_heap,0,expanded)==to;\n'
 '  if(valid)for(u32 z=0;z<from&&z<to;z++)if(expanded[z]!=(u8)((z*13+7)&255))valid=0;\n'
 '  if(valid)for(u32 z=from;z<to;z++)if(expanded[z])valid=0;\n'
 '  if(!free_block(private_heap,0,expanded)||!valid){print("NT_REALLOC_FAILED: data/size/free\\n");goto done;}\n'
 ' }\n'
 ' print("KB_NT_REALLOC_OK: 4 size pairs, content, extension zeros and free\\n");\n'
)
header=header.replace(key,key+addition)
(src/"nt_startup_wrapper_realloc_candidate.h").write_text(header,encoding="utf-8")
shell=(src/"shell_heap_policy_alloc_candidate.h").read_text(encoding="utf-8")
s_old='#include "nt_startup_heap_policy_alloc_candidate.h"'
assert shell.count(s_old)==1
(src/"shell_wrapper_realloc_candidate.h").write_text(shell.replace(s_old,'#include "nt_startup_wrapper_realloc_candidate.h"'),encoding="utf-8")
boot=(src/"boot_heap_policy_alloc_candidate.c").read_text(encoding="utf-8")
b_old='#include "shell_heap_policy_alloc_candidate.h"'
assert boot.count(b_old)==1
(src/"boot_wrapper_realloc_candidate.c").write_text(boot.replace(b_old,'#include "shell_wrapper_realloc_candidate.h"').replace("0.38-P2-test","0.38-P3-test"),encoding="utf-8")
pack=(root/"make_iso_heap_policy_alloc_candidate.py").read_text(encoding="utf-8")
assert "heap-policy-alloc" in pack
(root/"make_iso_wrapper_realloc_candidate.py").write_text(pack.replace("heap-policy-alloc","wrapper-realloc"),encoding="utf-8")
suite=(root/"test_console_heap_policy_alloc_candidate.py").read_text(encoding="utf-8")
assert "heap-policy-alloc" in suite and "console-heap-alloc-candidate" in suite
suite=suite.replace("heap-policy-alloc","wrapper-realloc").replace("console-heap-alloc-candidate","console-wrapper-realloc-candidate")
marker="                    assert 'NT_LFH_ALLOCATION_OK:' in startup_text, startup_text[-2500:]"
assert suite.count(marker)==1
suite=suite.replace(marker,marker+'\n                    assert "KB_NT_REALLOC_OK:" in startup_text and "KB_HEAP_SET_FAILURES: 0,122,0,31" in startup_text, startup_text[-2500:]')
(root/"test_console_wrapper_realloc_candidate.py").write_text(suite,encoding="utf-8")
print("P3 sources, isolated test harness and ISO packer prepared; P2 unchanged.")
