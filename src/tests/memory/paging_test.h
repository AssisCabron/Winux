/* Exercises CPU translation/TLB and permission transitions with reserved pages. */
static int paging_test(void){
 u8 *a=page_alloc(),*b=page_alloc();if(!a||!b){if(a)page_free(a);if(b)page_free(b);return 0;}
 u64 va=PG_WINDOW;int ok=1;volatile u8 *p=(volatile u8*)va;
 a[0]=17;b[0]=29;
 if(!pg_map(va,(u64)a,1,0))ok=0;
 if(ok){if(*p!=17)ok=0;*p=63;if(a[0]!=63)ok=0;}
 if(pg_map(va,(u64)b,1,0)||pg_map(va+1,(u64)b,1,0)||pg_map(0,(u64)b,1,0))ok=0;
 if(!pg_protect(va,0,0)||((*pg_leaf(va,0)&(PG_WRITE|PG_NX))!=PG_NX))ok=0;
 if(!pg_unmap(va)||pg_unmap(va)||pg_protect(va,1,0))ok=0;
 if(!pg_map(va,(u64)b,1,0))ok=0;
 if(ok&&*p!=29)ok=0; /* Same VA, different physical page: TLB invalidation. */
 const u8 code[]={0xb8,42,0,0,0,0xc3};for(u32 i=0;i<sizeof(code);i++)b[i]=code[i];
 if(!pg_protect(va,0,1))ok=0;
 __asm__ volatile("xor %%eax,%%eax; cpuid":::"rax","rbx","rcx","rdx","memory");
 if(ok&&((int(*)(void))va)()!=42)ok=0;
 if(!pg_unmap(va))ok=0;
 if(!page_free(a)||!page_free(b))ok=0;
 return ok;
}
static void paging_fault_test(int execute){
 u8 *a=page_alloc();if(!a||!pg_map(PG_WINDOW,(u64)a,1,0)){print("PAGE_TEST_SETUP_FAILED\n");return;}
 if(execute){a[0]=0xc3;serial("PAGE_TEST: NX instruction fetch\r\n");((void(*)(void))PG_WINDOW)();}
 else{pg_protect(PG_WINDOW,0,0);serial("PAGE_TEST: read-only write\r\n");*(volatile u8*)PG_WINDOW=1;}
 print("PAGE_TEST_PERMISSION_FAILED\n");pg_unmap(PG_WINDOW);page_free(a);
}
