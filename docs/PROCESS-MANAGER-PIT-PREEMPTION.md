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
- src/process/process_manager_preview.h: registro inicial de quatro slots
  gerenciados, estados FREE/READY/RUNNING/EXITED/FAILED, PIDs monotônicos,
  tabela NT de handles por processo, spawn de fixture CPL3, execução
  controlada, encerramento e reap de recursos. Não é um loader universal.
- src/process/timer_preemption_preview.h: controlador IRQ0 opt-in, quadro
  de interrupção de 15 GPRs + RIP/RFLAGS/RSP, retorno com IRETQ, contexto
  per-thread, CR3 específico, TSS.RSP0/stack privilegiada separada e FPU
  preservada com FXSAVE64/FXRSTOR64. Um IRQ em CPL0 apenas contabiliza ticks
  e retorna, sem escalonar kernel crítico.
- experiments/preemption/boot.c habilita tudo somente numa ISO isolada.
  A versão foundation experimental habilita os comandos de diagnóstico,
  mas a IDT original de IRQ0 só é trocada DURANTE o teste específico.
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
primeiro IRQ; 38 após a inclusão de pilhas/TSS e MXCSR; 39 com o
Process Manager real conectado e os dois ciclos repetidos.
Relatórios em build/console-timer-preemption-p4-test.json,
build/console-preempt-stacks-fpu-test.json,
build/console-managed-preemption-p4-test.json.

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

O agendamento pelo timer existe somente dentro do comando de prova; a IDT
é restaurada imediatamente. O scheduler cooperativo continua atendendo
serviços clockd/memd. O kernel é single-core, e apenas uma tarefa CPL3
entra de cada vez. Faltam:

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
