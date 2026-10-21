/* Winux native CPL3 ABI — experimental v1 surface with REAL consumers.
 * NEVER conflate with Microsoft's version-pinned NT service numbers.
 * Current preview uses INT 0x80; future SYSCALL implementation must preserve
 * these argument semantics and the kernel/user trust boundary.
 * x86_64 inputs: RAX=service, RDI,RSI,RDX=arguments; output RAX.
 */
#ifndef WINUX_NATIVE_SERVICES_V1_H
#define WINUX_NATIVE_SERVICES_V1_H
#define WX_NATIVE_ABI_VERSION 1U
#define WX_NATIVE_EXIT 0U            /* RDI = exit code. Does not return. */
#define WX_NATIVE_CONSOLE_WRITE 1U   /* RDI=stdout handle, RSI=user pointer, RDX=bytes. */
#define WX_NATIVE_YIELD 2U           /* Cooperative CPL3 switch: preserve trap frame and resume after int80. */
#define WX_NATIVE_CLOCK_TICKS 3U     /* Read real PIT monotonic tick counter; result in RAX. */
#define WX_NATIVE_MEMORY_FREE_PAGES 4U /* Free pages in existing physical pool, result in RAX. */
#define WX_NATIVE_IPC_SEND 5U /* RDI=target PID, RSI=64-bit value, RAX=1 accepted or 0 rejected. */
#define WX_NATIVE_IPC_RECEIVE 6U /* Returns one queued value, or 0 if empty; source PID comes from kernel. */
#define WX_NATIVE_STDOUT_HANDLE 1U
#define WX_NATIVE_MAX_WRITE 256U
#endif
