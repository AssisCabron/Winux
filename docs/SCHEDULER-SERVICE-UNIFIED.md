# Winux — Registro único de processos e IRQ0 residente

Data: 21/10/2026. Somente a candidata foundation-preview, não a ISO estável 0.38.

## Problema que esta migração elimina

As versões anteriores guardavam PID, CR3, quadro INT80, FXSTATE e pilha
privilegiada tanto em wx_managed_process como em wx_resident_service. Além
disso, clockd/memd eram selecionados por uma fila própria no gerenciador
de serviços, enquanto os demais processos usavam o Process Manager.

Isso criava duas fontes de verdade para o ciclo de vida da mesma tarefa.

## Arquitetura consolidada

- src/process/process_manager_preview.h: único registro com seis slots,
  quatro temporários (para processos criados e inteiramente liberados) e
  dois residentes (para clockd e memd).
- Um wx_managed_process detém identidade em execução, estado FREE/READY/
  RUNNING/EXITED/FAILED, CR3, tabelas de páginas, código e stack CPL3,
  tabela NT de handles, quadro completo de continuação de usuário,
  FXSTATE e pilha privilegiada de 16 KiB associada à thread.
- Os descritores wx_resident_service guardam somente metadados do serviço,
  métricas, IPC e supervisão, com um ponteiro para sua entrada gerenciada.
  Stop/Restart conservam a reserva de páginas dos dois serviços para
  reutilizá-la; processos temporários usam wx_proc_reap e liberam tudo.
- A função wx_proc_pick_ready define a ordem única round-robin de entradas
  READY e seus deadlines, usando o registro comum. A política de serviços
  não mantém uma segunda fila; wx_service_poll apenas supervisiona falhas
  e entrega o processo escolhido ao executor CPL3 existente.
- O comando ps enumera o registro comum, não uma lista paralela montada
  a partir de nomes de serviços. services exibe os metadados de clockd/memd.
- O backend de IRQ usa wx_proc_current_irq para identificar o contexto
  proprietário; jamais libera o contexto se esse ponteiro estiver ativo.

## IRQ0 persistente dentro da candidata

O handler wx_timer_irq_gate substitui o IRQ32 do PIC após o boot da ISO
experimental. Não é necessário executar preempt-test para instalá-lo.
O comando sched-status verifica o endereço efetivamente presente na IDT,
e mostra a origem do seletor READY.

Ao interromper CPL0, o handler somente atualiza o contador PIT e retorna.
Ao interromper CPL3, captura os 15 GPRs e a moldura de retorno de hardware,
restaura o contexto do kernel e devolve o controle ao escalonador; a próxima
retomada utiliza o CR3 e o FXSTATE da thread selecionada.

A arquitetura **continua single-CPU** e **não é uma preempção de threads de
kernel em CPL0**. Uma thread arbitrária de usuário ainda não pode ser
carregada de um PE genérico. As duas tarefas residentes usam programas
mínimos de serviço, e os testes de processos exercitam binários próprios.
O acesso a drivers SYS Windows não foi habilitado.

## Condições que precisam ser verdadeiras em testes

1. O marker PIT_IRQ_PERSISTENT_READY deve aparecer antes de aceitar comandos
   e o status enabled=1 deve permanecer após os demais testes.
2. Criação, parada, falha/restart e consultas dos dois serviços mantêm
   identidade PID/CR3/FXSTATE do Process Manager, sem slots duplicados.
3. Dois programas CPL3 que não chamam YIELD devem ser interrompidos pelo
   IRQ0, com recuperação de RIP, GPR, RFLAGS, stack privilegiada e MXCSR.
4. Os testes de isolamento NT, handles, FAT16, PCI/DMA e DLLs continuam
   aprovados dentro da mesma ISO.
5. As páginas de processos temporários, tabelas de páginas e recursos
   retornam à contagem anterior; os serviços mantêm somente sua reserva
   controlada, para reinício sem vazamentos progressivos.
6. A ISO 0.38 e a P4 histórica, além das imagens de disco descartáveis,
   têm hashes preservados.

Para reproduzir o pacote integrado:

    .\scripts\build\verify_foundations.ps1

Para testar apenas IRQ e alocação controlada:

    .\scripts\build\verify_process_preemption.ps1

Relatórios em build/console-foundations-p4-test.json,
build/console-foundations-p4-serial.log e
build/foundation-preview/foundation-preview-build.json.

## Limites antes de promover ao kernel principal

- Não existe SMP, APIC/IOAPIC, interrupções MSI-X, XSAVE/AVX completo, FS/GS
  e TEB generalizados, quantum/prioridades com políticas de espera,
  chamadas bloqueantes reais, sincronização multicore ou userland genérico.
- O código CPL3 entra e sai de um gate de kernel em uma única CPU; a
  camada CPL0 ainda não é preemptiva nem tolerante à migração entre CPUs.
- O VFS segue FAT16 readonly em imagem descartável; não carregar SYS
  desconhecidos sem WDM/Storport/IRP, política IOMMU e testes reais.
- Nem serviços originais Windows nem binários Microsoft foram copiados.
