# Winux — CPL3 cooperativo, ABI de troca de contexto, PCI PnP e DMA

Data: 21/10/2026. Alvo: somente as ISOs de experimento em build/.
Versões estáveis 0.38 e P4 históricas **não foram sobrescritas**.

## 1. Erros do escalonador corrigidos

### Fronteira C / assembly

O antigo corpo inline de wx_sched_switch, que consumia RCX/RDX
implicitamente, foi substituído por:

- src/arch/x86_64/context_switch.S — transição com convenção de registradores
  própria e explícita: RDI=ponteiro de origem, RSI=ponteiro de destino.
- src/process/scheduler_preview.h — wrapper C declarado ms_abi que posiciona
  os argumentos nesses dois registradores por constraints, sem supor qual
  registrador o compilador escolheu inicialmente para argumentos C.
- scripts/build/make_proc_space_preview.py — compila a unidade assembly via
  Zig build-obj e a vincula ao ELF/PE UEFI da candidata, somente quando a
  variante necessita de um escalonador.

O assembly preserva RBP, RBX, RDI, RSI, R12-R15, RFLAGS, RSP, CR3 e FXSTATE
(x87 e SSE) por tarefa. Não salva quadros de interrupção para preempção e
não implementa XSAVE/AVX. CR0/CR4, flags de interrupção e alinhamento da
área FXSAVE são verificados antes do teste.

### Layout inicial e contrato Win64

Com TOP alinhado a 16 bytes:

    TOP-1  ........ TOP-32 : shadow space reservado ao primeiro callee
    TOP-40                  : RSP que o callee verá na entrada (RSP mod 16 = 8)
    TOP-48                  : endereço wx_sched_entry, consumido por RET
    TOP-56                  : RFLAGS inicial 0x202, consumido por POPFQ
    TOP-120..TOP-57         : oito quadwords para POP dos registradores

Após oito POPs, POPFQ e RET, a entrada de wx_sched_entry satisfaz a ABI Win64.
O código terminado usa um yield final. Ele nunca é reagendado; caso seja
reativado por defeito, executa UD2 (falha evidente), em vez de HLT silencioso.
A stack, FXSTATE e a tabela de objetos são zeradas somente após o scheduler
retornar para sua própria pilha privilegiada.

### Memória e privilégios

Cada espaço de endereço possui PML4/PDPT/PD/PT próprio para a janela de
usuário. Code RX, stack RW/NX e dados do kernel continuam supervisor-only.
A verificação wx_sched_kernel_map_ok comprova que os endereços de funções,
tabelas de páginas, contextos, logs, objetos e pilhas do kernel estão
presentes nas raízes CR3 testadas e que os PDPTE/PDE do kernel não possuem
o bit USER. Sem essa herança de kernel supervisor, uma mera troca de CR3
geraria #PF no início do switch.

O helper wx_user_readable passou a consultar o CR3 ATUAL e a verificar os
bits USER/PRESENT em cada nível. O processo não pode usar as permissões
da tabela global de outro espaço para validar um ponteiro de usuário.

### Bug de RFLAGS descoberto pela integração CPL3

O primeiro teste de yield/resume com MXCSR/RFLAGS reprovaria mesmo quando o
primeiro usuário voltava corretamente com exit code 21. A causa era o
interrupt gate: INT 80h limpava IF no kernel e o atalho wx_ring3_restore
retornava ao scheduler sem recuperar as flags de antes do IRETQ.
O estado do kernel foi corrigido: wx_enter_user e wx_resume_user registram
o RFLAGS privilegiado antes de entrar em ring 3 e wx_ring3_restore
restaura esse valor com POPFQ antes do RET. Assim o kernel não continua
executando acidentalmente com interrupções desabilitadas.

O teste também atribui dois modos MXCSR distintos às tarefas e os confere
após dois ciclos de context switch. Esses são requisitos apenas para a
variante de uma CPU com FXSAVE; XSAVE/AVX/SMP e estados de interrupção de
um escalonador preemptivo permanecem fora do escopo.

Este primeiro desenho compartilha as regiões físicas de kernel entre
processos. Isso é deliberado em um kernel de espaço único com proteção
CPL0/CPL3, mas não representa isolamento entre drivers kernel.

### Execução CPL3 reagendável

A ABI nativa existente ganhou um consumidor real para WX_NATIVE_YIELD=2,
via INT 80h. No preview integrado, duas tarefas alternam as execuções
11/22, 13/24 e 15/26. No segundo turno cada uma entra de fato em ring 3
e chama YIELD. O dispatcher guarda 15 GPRs e RIP/RSP/RFLAGS de seu trap frame.
No terceiro turno, wx_resume_user restaura o quadro e usa IRETQ para
recomeçar **depois** da INT 80h, no CR3 daquela tarefa; os dois processos
encerram com 21 e 42, sem compartilhar pilha ou páginas de usuário.
Os callbacks de objetos NT usam tabelas próprias por tarefa, rejeitando
handles inválidos e fechando recursos antes do teardown.

**Limites não resolvidos:** o scheduler atual é cooperativo e usa uma CPU,
a transição TSS/RSP0 é compartilhada ao longo de testes serializados.
Preempção por IRQ, salvamento independente do trap por thread, TSS/RSP0
por CPU, exclusão mútua em SMP, DPC, prioridades, bloqueio em espera,
endereços userland de processos arbitrários e sessões autenticadas
AINDA NÃO foram implementados. Nunca transportar este código diretamente
para um scheduler preemptivo sem os correspondentes quadros de interrupção.

## 2. Subsistema comum PCI PnP e DMA

- src/device/pci_pnp.h enumera funções PCI via mecanismo de configuração 1,
  coleta Vendor/Device ID, BDF, class/subclass/progif, BAR e IRQ, permite
  reivindicação única, vincula por tabela de drivers nativos e recusa
  IDs desconhecidos. É um começo reutilizável para storage/network.
- src/device/dma.h registra buffers conhecidos, dono PCI, direção e tamanho,
  recusa ponteiros não registrados, cruzamentos de limites e endereços acima
  de 4 GiB. Desvincular um dispositivo remove suas regiões.
- src/storage/block_core.h continua sendo a interface comum de blocos,
  sem duplicar VFS, tabelas de objetos nem scheduler.
- src/storage/virtio_blk_legacy.h passa a registrar um driver de
  Vendor 1AF4, Device 1001, usar o enumerador PCI, reservar buffers com
  wx_dma_register e obter endereços de barramento exclusivamente por
  wx_dma_bus_address antes de entregar descritores VirtIO à fila.

O teste QEMU efetua DMA real para leitura dos setores 0 e 16 de um disco
de 2 MiB aberto SOMENTE-LEITURA, verifica assinatura, bytes, repetições,
recusa leituras fora do volume, drivers com IDs não correspondentes e
mapeamentos DMA indevidos. A imagem de disco tem SHA-256
eec5491bdd28c298fd765a499639efcd6a0e4f248cccff885a1f45b61c611f6b,
inalterado após o teste.

**Importante:** a lista DMA é uma fronteira de validação de API contra
chamadas acidentais. Como ainda não há IOMMU, ela NÃO impede que um
dispositivo PCI físico malicioso faça DMA fora dessas regiões. Não existe
virtualização DMA/MDL/IOMMU para drivers Windows, nem hotplug ou IRP/PnP
do Windows. Um arquivo SYS não passa a funcionar só por estar presente
no diretório SYSTEM/DRIVERS. Para rede Windows ainda faltam NDIS e os
contratos de lifecycle específicos de adaptadores.

## 3. Como validar

Na raiz do repositório Winux:

    .\scripts\build\verify_foundations.ps1 -Python "C:\Program Files\Python310\python.exe"

O comando reconstrói o mesmo kernel P4 de forma isolada com os módulos
acima, executa a regressão completa QEMU, compara a ISO antiga e preserva
o conteúdo e o hash do disco de teste. Evidências:

- build/foundation-preview/winux-0.38-wrapper-fls-candidate.iso
- build/console-foundations-p4-test.json
- build/console-foundations-p4-serial.log
- build/foundation-preview/foundation-preview-build.json

O status "passed" de QEMU valida **somente** os caminhos testados.
Não valida drivers Windows SYS, SSDs físicos, PnP ACPI/hotplug, interfaces
de rede ou preempção em processos gerais. A prioridade futura é provar a
preempção de uma tarefa CPL3 interrompida pelo timer, com trap frame
completo por thread, DMA/IOMMU apropriados e binding PnP assíncrono,
antes de testar uma versão/licença conhecida de miniport Storport real.
