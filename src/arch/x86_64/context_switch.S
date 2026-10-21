/* Assembly-only register contract: RDI=from, RSI=to. Build/link COFF for UEFI x64. */
.text
.globl wx_sched_switch_regs
wx_sched_switch_regs:
  pushfq
  cli
  pushq %rbp
  pushq %rbx
  pushq %rdi
  pushq %rsi
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
  movq %rsp,0(%rdi)
  movq %cr3,%rax
  movq %rax,8(%rdi)
  fxsave64 16(%rdi)
  movq 8(%rsi),%rax
  movq %rax,%cr3
  fxrstor64 16(%rsi)
  movq 0(%rsi),%rsp
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %rsi
  popq %rdi
  popq %rbx
  popq %rbp
  popfq
  retq
