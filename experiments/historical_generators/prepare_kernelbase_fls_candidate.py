"""P4 candidate: original KernelBase FLS wrappers plus native-context cleanup."""
from pathlib import Path
root=Path(__file__).resolve().parent
src=root/"src"
header=(src/"nt_startup_wrapper_realloc_candidate.h").read_text(encoding="utf-8")
begin='static int nt_startup_test(void){'
assert header.count(begin)==1
hook=(
 'static unsigned kb_fls_count;\n'
 'static u64 kb_fls_last;\n'
 'static void kb_fls_callback(void *value){kb_fls_count++;kb_fls_last=(u64)value;}\n'
)
header=header.replace(begin,hook+begin)
marker=' print("KB_NT_REALLOC_OK: 4 size pairs, content, extension zeros and free\\n");\n'
assert header.count(marker)==1
probe=(
 ' /* Original KernelBase FLS wrappers, backed by the version-matched NTDLL. */\n'
 ' u32(*fls_alloc)(void(*)(void*))=(void*)module_export(k,"FlsAlloc",0,0);\n'
 ' int(*fls_set)(u32,void*)=(void*)module_export(k,"FlsSetValue",0,0);\n'
 ' void*(*fls_get)(u32)=(void*)module_export(k,"FlsGetValue",0,0);\n'
 ' int(*fls_free)(u32)=(void*)module_export(k,"FlsFree",0,0);\n'
 ' if(!fls_alloc||!fls_set||!fls_get||!fls_free)goto done;\n'
 ' kb_fls_count=0;kb_fls_last=0;\n'
 ' u32 first_index=fls_alloc(kb_fls_callback);\n'
 ' if(first_index==0xffffffffU||!fls_set(first_index,0))goto done;\n'
 ' last_error=123;void *fls_empty=fls_get(first_index);\n'
 ' if(fls_empty||last_error)goto done;\n'
 ' if(!fls_set(first_index,(void*)0x1111)||fls_get(first_index)!=(void*)0x1111||last_error)goto done;\n'
 ' if(!fls_set(first_index,(void*)0x2222)||kb_fls_count||fls_get(first_index)!=(void*)0x2222)goto done;\n'
 ' if(!fls_free(first_index)||kb_fls_count!=1||kb_fls_last!=0x2222)goto done;\n'
 ' last_error=0;if(fls_get(~0U)||last_error!=87)goto done;\n'
 ' last_error=0;if(fls_set(~0U,(void*)0x1111)||last_error!=87)goto done;\n'
 ' last_error=0;if(fls_free(~0U)||last_error!=87)goto done;\n'
 ' u32 pending_index=fls_alloc(kb_fls_callback);\n'
 ' if(pending_index==0xffffffffU||!fls_set(pending_index,(void*)0x3333))goto done;\n'
 ' print("KB_FLS_WRAPPERS_OK: original KB alloc/get/set/free, LastError and callback\\n");\n'
)
header=header.replace(marker,marker+probe)
end=' nt_native_leave(&scope);if(!nt_native_dispose())return 0;\n'
assert header.count(end)==1
header=header.replace(end,end+
 ' if(kb_fls_count!=2||kb_fls_last!=0x3333||rd64(app_teb+0x17c8))return 0;\n'
 ' print("KB_FLS_TEARDOWN_OK: callback on context disposal, TEB cleared\\n");\n')
(src/"nt_startup_wrapper_fls_candidate.h").write_text(header,encoding="utf-8")
shell=(src/"shell_wrapper_realloc_candidate.h").read_text(encoding="utf-8")
old='#include "nt_startup_wrapper_realloc_candidate.h"'
assert shell.count(old)==1
(src/"shell_wrapper_fls_candidate.h").write_text(shell.replace(old,'#include "nt_startup_wrapper_fls_candidate.h"'),encoding="utf-8")
boot=(src/"boot_wrapper_realloc_candidate.c").read_text(encoding="utf-8")
old='#include "shell_wrapper_realloc_candidate.h"'
assert boot.count(old)==1
(src/"boot_wrapper_fls_candidate.c").write_text(boot.replace(old,'#include "shell_wrapper_fls_candidate.h"').replace("0.38-P3-test","0.38-P4-test"),encoding="utf-8")
iso=(root/"make_iso_wrapper_realloc_candidate.py").read_text(encoding="utf-8")
assert "wrapper-realloc" in iso
(root/"make_iso_wrapper_fls_candidate.py").write_text(iso.replace("wrapper-realloc","wrapper-fls"),encoding="utf-8")
suite=(root/"test_console_wrapper_realloc_candidate.py").read_text(encoding="utf-8")
assert "wrapper-realloc" in suite
suite=suite.replace("wrapper-realloc","wrapper-fls")
old='                    assert "KB_NT_REALLOC_OK:" in startup_text and "KB_HEAP_SET_FAILURES: 0,122,0,31" in startup_text, startup_text[-2500:]'
assert suite.count(old)==1
suite=suite.replace(old,old+'\n                    assert "KB_FLS_WRAPPERS_OK:" in startup_text and "KB_FLS_TEARDOWN_OK:" in startup_text, startup_text[-2500:]')
(root/"test_console_wrapper_fls_candidate.py").write_text(suite,encoding="utf-8")
print("Isolated P4 generated, prior versions untouched.")
