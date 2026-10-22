# Winux — isolamento CPL3 e fronteira DMA

Data: 21/10/2026. Escopo experimental: Foundation Preview em QEMU/1 vCPU.

## Isolamento de CPU

- Os processos usam tabelas PML4/PDPT/PD/PT independentes com CR3 próprio.
  A região de kernel, compartilhada, permanece supervisor-only.
- src/arch/x86_64/ring3_preview.h valida cada página de um buffer recebido
  da syscall contra o CR3 ATIVO, exigindo bits Present e User em todos os
  níveis; recusa tamanho nulo, overflow, buracos e memória de kernel.
- Código CPL3 fica RX (sem escrita) e pilhas de usuário RW/NX.
- src/tests/process/isolation_dma_test.h cria dois processos reais,
  testa os mesmos endereços virtuais com frames físicos diferentes e
  executa código CPL3 tentando ler a página física do outro processo e
  a área DMA física do kernel. O esperado é uma #PF com bits Present/User,
  sem reset da máquina. O teardown deve recuperar quadros e handles.
- O teste também provoca duas falhas reais de proteção: tentativa CPL3 de
  **escrever na própria página de código RX** (erro de página Present/Write/User)
  e de **executar a pilha RW/NX** (erro de página Present/User/Instruction).
  Ambas terminam somente o contexto de teste, sem queda do kernel.
- Os testes são repetidos, não apenas uma análise estática de CR3.

Isso NÃO torna um driver CPL0 isolado: todo código privilegiado continua
capaz de acessar as regiões de kernel e quadros físicos de processos.

## DMA dedicado a drivers nativos confiáveis

src/device/dma.h disponibiliza um pool de 32768 bytes exclusivo para
DMA coerente. wx_dma_alloc reserva intervalos alinhados e associa cada
intervalo ao proprietário PCI, direção e tamanho, sem sobreposição.
wx_dma_bus_address recusa endereços, direções ou owner inválidos;
wx_dma_register recusa páginas de kernel, stacks e quadros de usuário
porque eles ficam fora desse pool explícito.

O driver de blocos VirtIO legado usa essa API para a fila de descritores,
request header, bounce de setor e byte de status. No shutdown, o
dispositivo é resetado, a entrada de VFS/blocos é invalidada, o bit de
PCI bus mastering é desligado ANTES de revogar e zerar o pool DMA.
O teste de ativação tardia do bit PCI BUS_MASTER foi tentado, mas
**não foi validado no QEMU**: durante o probe, o registro PCI já retornava
BUS_MASTER=1 antes da ativação explícita final. Para preservar a leitura
do dispositivo, o driver voltou ao sequenciamento de inicialização
já validado, com reset VirtIO, buffers dedicados e revogação segura.
Não há garantia de que o controlador não possa iniciar DMA durante o probe.

Após remover o controlador, a configuração PCI é restaurada EXCETO o bit
BUS_MASTER, que permanece desligado (*fail closed*). Só um novo probe
autorizado pode habilitá-lo novamente. O DMA usa um token de **geração
monotônica** associado a cada intervalo; uma referência do probe anterior
é rejeitada mesmo que o buffer volte a ocupar o mesmo endereço físico.
Um novo probe readquire o mesmo pool, sem uso crescente de memória.

Provas: DMA_APERTURE_OK, DMA_REVOKE_OK, DMA_GENERATION_OK,
DMA_MASTER_GUARD_OK, DMA_BUSMASTER_OFF_OK, PNP_DMA_GUARD_OK,
CPU_ISOLATION_OK, CPU_WX_NX_OK e ISOLATION_REAP_OK no log QEMU.

As verificações DMA foram executadas com leitura real via VirtIO-blk após
ExitBootServices, incluindo detach/reprobe, limpeza de buffer e falha de
consulta a endereço DMA antigo. Um teste isolado no QEMU passou em 40
grupos, com dois ciclos de tentativas de acesso CPL3 indevido.

## Restrição de segurança fundamental

Esta é uma ALLOWLIST EM SOFTWARE para drivers próprios que respeitam
a API. O token de geração impede referências obsoletas da API, mas NÃO
é um segredo de segurança e não impede acesso direto por código CPL0.
Ainda NÃO há domínio Intel VT-d ou AMD-Vi configurado.
Um controlador hostil pode ignorar a API e fazer DMA diretamente em
qualquer endereço físico acessível. Não carregar SYS de terceiros em
CPL0 nem confiar em isolamento DMA físico até implementar IOMMU real,
remapeamento/invalidacao por dispositivo e tratamento de faults.

Também faltam: scatter-gather, MDL, memória DMA acima de 4 GiB,
IRQ/MSI-X, hotplug, SMP, estados de FPU por CPU e suporte AHCI/NVMe.

## Reproduzir

Na raiz Winux:

    .\scripts\build\verify_foundations.ps1

A saída precisa indicar a regressão aprovada, e os hashes de ISO 0.38,
P4 histórica, disco FAT16 antigo e árvore FAT16 devem permanecer iguais.
Os artefatos são:
build/console-foundations-p4-test.json
build/console-foundations-p4-serial.log
build/foundation-preview/foundation-preview-build.json

A verificação é uma prova das rotas exercitadas, não de compatibilidade
universal com drivers Windows, dispositivos PCI ou isolamento hostil.
