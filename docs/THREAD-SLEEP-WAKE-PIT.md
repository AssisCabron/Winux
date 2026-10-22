# Winux — Thread sleep/wake via PIT

Data: 21/10/2026. Only experimental Foundation Preview.

## Native ABI

WX_NATIVE_SLEEP_TICKS = 7. RDI accepts 1..1000 PIT ticks, and rejects
invalid or overflow durations. Returns -EINVAL without suspending on
invalid requests; on successful resumption the guest receives RAX=0.
Uses the EXISTING INT 80h gate and wx_user_resume_frame in its owner.
No duplicate syscall table or Windows NTDLL service numbers.

## Managed lifecycle

The authoritative wx_process_table gained WX_PROC_BLOCKED. A process
stores its own next_ready_tick. wx_proc_request_sleep validates caller
context and transitions RUNNING -> BLOCKED. wx_proc_wake_blocked traverses
all managed processes and promotes expired waits to READY, using genuine
timer_ticks. The existing round-robin picker may select only READY.

Both clockd (PIT monitor) and memd (free page monitor) now block for ten
ticks after work, instead of voluntarily yielding on each quantum.
Their owned CR3, FXSTATE and thread frames are retained. Stop can resume a
blocked process immediately to reach its genuine EXIT syscall; restart
reuses the slot without consuming more pages.

## Verification

- sched-status shows blocked/sleeps/wakes and ready queue origin.
- wait-test validates invalid durations, BLOCKED until actual PIT deadline,
  eventual READY, termination and resource reaping.
- --check-sleep checks actual CPL3 services making sleep syscalls, the PIT
  resuming them and both PIDs still alive after idle windows.
- verify_foundations.ps1 runs these along with IRQ0, IPC, FAT16 and PCI tests.

## Remaining limits

Single CPU; no general kernel preemption, SMP/APIC, event-based waits,
I/O completion queues, multiple threads in one address space, sleeping
inside the Win32 runtime, generalized priorities or Windows SYS drivers.
