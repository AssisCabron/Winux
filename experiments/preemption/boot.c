/* Dedicated IRQ0 preemption experiment; never enabled in stable 0.38/P4. */
#define WINUX_RING3_PREVIEW 1
#define WINUX_PROC_SPACE_PREVIEW 1
#define WINUX_PROC_MANAGER_PREVIEW 1
#define WINUX_SCHED_PREVIEW 1
#define WINUX_USER_SCHED_PREVIEW 1
#define WINUX_TIMER_PREEMPT_PREVIEW 1
#include "../candidates/p4/boot.c"
