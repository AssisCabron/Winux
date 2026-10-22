# Winux — threads CPL3, suspensão e ciclo de vida

Data da implementação: 2026-10-21.
Escopo: Foundation Preview isolada, QEMU com um vCPU.

## Arquitetura usada

Não foram criados um segundo kernel, outro scheduler ou outro alocador.
O Process Manager já existente em src/process/process_manager_preview.h
continua responsável por PID, CR3, tabelas NT de handles e memória física.

A thread principal usa wx_proc_thread, com frame x86-64, FXSTATE,
pilha privilegiada RSP0 e contadores próprios. O experimento agora pode
instalar uma segunda thread dentro do MESMO processo, reutilizando as
tabelas de páginas, código, objetos e identidade do processo. O segundo
contexto possui TID diferente, pilha de usuário em uma página distinta,
outra pilha RSP0 e FXSTATE independente.

O kernel só libera a página adicional da segunda thread depois de
voltar a uma pilha privilegiada segura e confirmar que a thread não está
em execução nem é referenciada pelo IRQ0. O Process Manager remove
todas as páginas do processo, incluindo as quatro tabelas de endereço,
na destruição do seu registro.

## Suspensão e despertar

A ABI nativa inclui WX_NATIVE_SLEEP_TICKS=7, com argumento RDI=1..1000.
O gate INT80 guarda a continuação do usuário e muda o estado do processo
de RUNNING para BLOCKED. O processo usa next_ready_tick dentro da mesma
estrutura gerenciada: não existe uma segunda fila ou sistema de timers.

wx_proc_wake_blocked usa os ticks reais do PIT para promover BLOCKED
a READY. O seletor round-robin escolhe apenas processos READY, sem
agendar threads que ainda esperam seu prazo. Os serviços clockd/memd
realmente executam essa syscall, aguardando dez ticks após cada ciclo.
Comandos stop/restart conseguem acordar um serviço em espera para que
ele alcance a sua syscall EXIT.

## Provas QEMU

- wait-test: rejeita espera zero ou superior a 1000 ticks, exige
  BLOCKED antes do prazo real, acorda por PIT, verifica READY e devolve
  as páginas/handles de um processo temporário.
- sched-status: publica a contagem de suspensões e despertares reais.
- threads-test: cria três processos sequenciais, cada um com duas threads
  CPL3, totalizando seis threads por execução. Cada dupla compartilha
  CR3/código/handles mas possui TIDs, stacks de usuário, quadros GPR,
  FXSTATE e RSP0 próprios. O código faz YIELD e EXIT a partir do próprio
  programa; cada thread retorna um código diferente e preserva seu
  modo de arredondamento MXCSR individual.
- O harness repete threads-test duas vezes no mesmo boot e exige
  liberação de todas as páginas físicas e tabelas permanentes.
- A regressão integrada inclui FAT16 readonly, PCI/VirtIO/DMA, IRQ0,
  serviços IPC e recuperação de falhas sem executáveis Microsoft.

Comando de validação desde a raiz Winux:

    .\scripts\build\verify_foundations.ps1 -Python "C:\Program Files\Python310\python.exe"

Evidências:
    build/console-foundations-p4-test.json
    build/console-foundations-p4-serial.log
    build/foundation-preview/foundation-preview-build.json

## Diagnóstico de threads reais

Os comandos ps e threads consomem diretamente wx_process_table.
ps mantém a indicação de ciclo de vida RUNNING para processos residentes,
mas acrescenta sched_state=BLOCKED/READY/RUNNING e tid para mostrar
o estado efetivo de agendamento. threads enumera os TIDs gerenciados,
seus PIDs, estados e RSP0, sem criar uma segunda lista decorativa.
O teste QEMU verifica que clockd/memd possuem TIDs distintos e que
os estados observados correspondem às entradas existentes.

## Limites sem disfarce

O uso de duas threads em um processo foi comprovado no teste controlado,
mas ainda não existe uma syscall genérica CreateThread, loader ELF/PE
arbitrário de CPL3, compartilhamento de heap de usuário, TLS, FS/GS,
múltiplas pilhas de usuário alocadas dinamicamente, eventos bloqueantes,
prioridades, SMP, IOMMU ou interrupções de kernel que possam ser
reagendadas em CPL0.

O teste de threads utiliza alternância pelo trap INT80. A preempção
IRQ0 é validada separadamente, também com contextos por processo, mas a
política de scheduler ainda não é uma implementação de produção para
qualquer binário de usuário.
