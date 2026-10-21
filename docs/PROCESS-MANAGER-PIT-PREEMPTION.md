# Winux — Process Manager e preempção PIT/IRQ0

Data: 21/10/2026 — estágio experimental, não promovido à ISO estável 0.38.

## O que foi implementado

O gerenciador inicial reutiliza a ABI CPL3, o dispatcher INT 80h, o alocador
de páginas, a estrutura wx_address_space e as tabelas NT de objetos existentes.

- src/process/address_space_preview.h: wx_space_make cria quatro páginas
  de tabela (PML4/PDPT/PD/PT), código e stack no pool físico com ownership
  explícito. wx_space_destroy libera as seis páginas SOMENTE depois do
  retorno ao contexto privilegiado e do CR3 original. Não consome mais
  permanentemente os 64 slots pg_new destinados ao kernel.
- src/process/process_manager_preview.h: registro único de seis slots
  (quatro transitórios e dois residentes), estados FREE/READY/RUNNING/EXITED/FAILED, PIDs monotônicos,
  tabela NT de handles por processo, spawn de fixture CPL3, execução
  controlada, encerramento e reap de recursos. Não é um loader universal.
- src/process/timer_preemption_preview.h: controlador IRQ0 persistente na ISO foundation experimental, quadro
  de interrupção de 15 GPRs + RIP/RFLAGS/RSP, retorno com IRETQ, contexto
  per-thread, CR3 específico, TSS.RSP0/stack privilegiada separada e FPU
  preservada com FXSAVE64/FXRSTOR64. Um IRQ em CPL0 apenas contabiliza ticks
  e retorna, sem escalonar kernel crítico.
- experiments/preemption/boot.c mantém um experimento isolado.
  Na versão foundation experimental, o IRQ0 é instalado após o boot
  e permanece ativo; o diagnóstico preempt-test restaura o handler
  que já estava instalado antes de seu próprio teste.
- scripts/build/verify_process_preemption.ps1: compila e valida a candidata,
  exige dois ciclos com processos não cooperativos e preserva ISOs antigas.

## Provas de comportamento

O fixture CPL3 usa uma instrução de laço que NÃO chama YIELD. O IRQ de
hardware 32 (PIT/IRQ0) salva a CPU, entrega o controle ao scheduler e só
então o kernel seleciona outro CR3 e outra thread. A retomada ocorre na
instrução interrompida; cada thread só sai quando seu próprio flag de stack
é marcado no kernel. As duas retornam com códigos 21/42.

O teste também programa MXCSR diferentes nas duas threads DIRETAMENTE
em CPL3; valores distintos precisam sobreviver ao IRQ, à troca de CR3
e ao IRETQ. Os quadros da interrupção precisam estar em duas stacks
privilegiadas diferentes, selecionadas via TSS.RSP0. RFLAGS, IF e IOPL
são verificados; RAX não pode ser zerado por um IRQ involuntário.

O Process Manager roda 12 processos por ciclo, quatro slots de cada
vez. Todos são executados em CPL3, reaped e suas páginas físicas,
tabelas de página e handles são devolvidos. A suíte repete o processo
duas vezes, totalizando 24 instâncias, exigindo que o total de páginas
físicas livres volte ao valor anterior.

As provas foram exercitadas isoladamente no QEMU: 38 grupos no
primeiro IRQ; 38 após a inclusão de pilhas/TSS e MXCSR; **39/39** com o
Process Manager conectado e os dois ciclos repetidos.
A regressão integrada mais recente passou em **55/55 grupos QEMU**, com
serviços residentes no mesmo Process Manager, IPC, supervisão, FAT16,
PCI/DMA, NT e IRQ0 persistente no mesmo boot.
Relatórios em build/console-timer-preemption-p4-test.json,
build/console-preempt-stacks-fpu-test.json,
build/console-managed-preemption-p4-test.json e
build/console-foundations-p4-test.json.
As identidades SHA-256 atuais são registradas a cada compilação em
build/preemption-preview/preemption-preview-build.json e
build/foundation-preview/foundation-preview-build.json. A última ISO
integrada validada possui SHA-256
7f666f4d2b745d32de7fbcc1994598cab2e5683b30ea6cb087942a0dee299eb8.

## Consolidação dos contextos por processo

A implementação original possuía o registro wx_process_table, mas mantinha
quadros de registradores, buffers FXSAVE e pilhas RSP0 em três arrays
independentes indexados pela ordem do fixture. Isso não permitia controlar
ownership do contexto pelo PID ao generalizar o escalonamento.

O wx_managed_process agora possui um wx_proc_thread com:

- frame completo de continuação CPL3 (15 GPRs, RIP/RSP/RFLAGS);
- FXSTATE alinhado a 16 bytes e preservado por tarefa;
- pilha de entrada privilegiada RSP0 de 16 KiB;
- contadores de interrupção e retomada por thread.

A entrada do IRQ0 consulta wx_proc_current_irq, que aponta para o mesmo
wx_managed_process gerenciado por wx_proc_spawn e wx_proc_reap. O handler
registra o frame de interrupção no contexto do processo interrompido, e o
scheduler usa o contexto desse mesmo processo para trocar CR3 e retomar
por IRETQ. Não existe mais vetor paralelo de contextos de usuário.

A função wx_proc_reap se recusa a liberar um processo enquanto a entrada de
IRQ possui seu contexto ativo. O teste verifica que duas tarefas são
interrompidas e retomadas, mantendo MXCSR independentes; após o encerramento,
a contagem de páginas físicas livres e a quantidade de tabelas permanentes do
kernel devem retornar exatamente aos valores da entrada. O teste é executado
duas vezes dentro do mesmo boot.

O contexto IRQ pertence ao processo gerenciado. Na ISO foundation
experimental, a IDT mantém o PIT preemptivo ativo durante a sessão inteira;
sem SMP, preempção de CPL0 ou escalonamento geral de aplicações externas.

## Como repetir

Na pasta Winux:

    .\scripts\build\verify_process_preemption.ps1 -Python "C:\Program Files\Python310\python.exe"

ISO do experimento:

    build/preemption-preview/winux-0.38-wrapper-fls-candidate.iso

O build integrado permanece separado:

    .\scripts\build\verify_foundations.ps1

A ISO estável 0.38 e a P4 histórica são verificadas por SHA-256 antes e
depois de construir as variantes.

## Limites: ainda não é um scheduler de produção

Na candidata foundation, o timer permanece instalado após o boot e o
Process Manager seleciona processos READY. As threads de kernel em CPL0
não são preemptadas; processos CPL3 entram uma de cada vez na CPU.
A ISO estável 0.38 não recebeu essa configuração. Faltam:

- integrar IRQ preemption ao loop geral de ready-queues, estados BLOCKED,
  prioridades, sleep/wait e policy de quantum;
- contextos completos de threads arbitrárias, FS/GS bases, TLS, TSS/RSP0
  por CPU, XSAVE/AVX e tratamento de signal/exception/unwind;
- PIDs e handles resistentes a reutilização indevida após overflow;
- hierarquia parent/child, criação a partir do loader PE real, IPC/ACL/
  tokens e sessões de usuário;
- espera de I/O, IRQ/MSI-X, locks/IRQL, race tests e SMP.

Cada evolução exige verificação diferencial contra os contratos Windows
quando o projeto executar código Win32/NT e provas de segurança no QEMU.
Copiar arquivos .sys não substitui IRP, PnP, Storport nem NDIS.
