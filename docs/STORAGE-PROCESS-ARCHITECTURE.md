# Winux — armazenamento, processos e compatibilidade de drivers Windows

Estado em 21/10/2026: experiências executadas em QEMU, não uma ISO estável.

## Integração comprovada dos três subsistemas

A ISO experimental conjunta reutiliza a implementação P4 original, o backend
de bloco, o Ring 3, duas raízes de CR3, o escalonador cooperativo e o
gerenciador de objetos já existente. Entrada: experiments/integration/boot.c.
Não existe uma segunda versão desses componentes.

- build/foundation-preview/winux-0.38-wrapper-fls-candidate.iso
- A identidade da ISO experimental e gerada a cada build e consta em build/foundation-preview/foundation-preview-build.json.
- Resultado atual: build/console-foundations-p4-test.json = passed:true, 41 grupos (CPL3 yield/resume, FPU e PnP/DMA)
- Log: build/console-foundations-p4-serial.log confirma BLOCK_READ_OK,
  SCHED_CONTEXT_OK, SCHED_OBJECT_NAMESPACE_OK e PROC_SPACE_OK.
- O disco de teste manteve SHA-256
  eec5491bdd28c298fd765a499639efcd6a0e4f248cccff885a1f45b61c611f6b.

Para repetir o ciclo integrado em uma só chamada, use:

    .\scripts\build\verify_foundations.ps1 -Python "C:\Program Files\Python310\python.exe"

O script confirma a identidade das ISOs anteriores e a imutabilidade da
imagem de disco de 2 MiB. O controle de drivers SYS Windows, preempção por
timer, acesso a volumes reais e gerenciamento de sessões seguem pendentes.


## O que está comprovado

### Armazenamento real após ExitBootServices

- src/storage/block_core.h: contrato de bloco independente do controlador;
  callback de leitura, capacidade, setor de 512 bytes, bounds e somente leitura.
- src/storage/virtio_blk_legacy.h: controlador PCI VirtIO legado 1AF4:1001;
  descoberta BDF, barramento, fila de descritores e DMA em memória estática
  identitária abaixo de 4 GiB. Acesso real depois da saída do firmware UEFI.
- experiments/block/boot.c: compila este backend somente para a ISO de teste.
- scripts/build/make_block_fixture.py: disco descartável de 2 MiB; recusa
  sobrescrever o disco existente. Não acessa discos físicos.
- scripts/build/make_block_preview.py: reutiliza P4 e o empacotador UEFI.
- tests/runners/test_console_wrapper_fls_candidate.py --block-image:
  anexa a imagem como somente leitura, testa setores 0 e 16, conteúdo,
  leituras repetidas e recusa de LBA inválido.

Resultado: 38 grupos da regressão QEMU aprovados, sem mudança no SHA-256
da imagem de disco. Resultado em build/console-block-p4-test.json.
ISO em build/block-preview/winux-0.38-wrapper-fls-candidate.iso,
SHA-256 178d2c43f1b188666b7f5595744a1b447354ea56ccc0e1b53c13bad5caf3c3f1.

Isso NÃO é driver SATA/AHCI/NVMe, VFS, volume montado ou pilha Storport.
O dispositivo testado é VirtIO-blk PCI legado, um request serializado de cada
vez, somente leitura, sem hotplug e sem domínio IOMMU por dispositivo.

### Memória, processos e escalonamento

- src/process/address_space_preview.h: dois PML4/PDPT/PD/PT distintos,
  mesmo VA com páginas físicas diferentes, kernel supervisor-only, CPL3
  emite exit codes 21, 42 e 21, liberando os recursos após o teste.
- src/process/scheduler_preview.h: duas tarefas de kernel com stacks
  próprias, preservando registradores Win64, FXSTATE/FPU e CR3.
  A alternância é REAL, cooperativa, com 6 yields no padrão
  11, 22, 13, 24, 15, 26; não depende de logs artificiais.
- Reutiliza obj_table do gerenciador NT já presente. Cada tarefa recebe
  tabela de handles própria. Um evento não aparece na tabela vazia de outra,
  alterações no próprio evento não vazam para a outra tabela, e o teardown
  não deixa handles vivos.
- experiments/proc_space/boot.c e experiments/scheduler/boot.c ativam somente
  os experimentos; a P4 estável continua inalterada.
- scripts/build/make_proc_space_preview.py contém um único packer com
  --experiment proc_space ou --experiment scheduler.
- Comandos no console da imagem experimental: proc-space-test e sched-test.

Resultado do escalonador com espaços de endereço: 39 grupos QEMU aprovados.
Ver build/console-scheduler-resources-p4-test.json e log serial.
ISO experimental em build/scheduler-preview/winux-0.38-wrapper-fls-candidate.iso
SHA-256 a4084e86bbb49136ed33459dc95c505519f8c41a1adb64ee9ea2a142730b5d17.

Limite: as duas tarefas que cedem CPU executam em CPL0; os programas CPL3
são testes sequenciais. Ainda não existe um escalonador preemptivo de
threads de usuário, bloqueios/esperas integrados, multicores, privilégios
por sessão nem proteção de recursos contra código malicioso em ring 0.
O isolamento de handle foi exercitado no núcleo de objetos, mas os
adaptadores NT ainda precisam despachar por processo corrente.

## Por que não basta copiar os drivers SYS do Windows

A pilha de armazenamento Windows possui WDM/WDF, DriverEntry, DeviceObject,
IoCreateDevice, IRPs, PnP/Power, DMA/MDL/IOMMU, spinlocks, níveis IRQL,
threads, DPCs, registro/INF, Storport/ScsiPort, classpnp, disk/partmgr,
mountmgr/volmgr, arquivos, cache, flush e tratamento de falhas.

Um driver de rede usa NDIS ou NetAdapterCx; copiar o binário para
SYSTEM/DRIVERS não faz esses contratos existirem. Os binários SYS
de terceiros e da Microsoft têm licenças que devem ser respeitadas.
Não chamar DriverEntry de um SYS arbitrário em CPL0.

A arquitetura pretendida conecta controladores por interfaces comuns,
sem criar um segundo VFS, gerenciador de objetos ou escalonador.
O código-fonte DuetOS é referência arquitetural; não houve cópia
de implementação dos drivers do DuetOS ou da Microsoft.
O REA continua sendo utilizado para mapear contratos reais.

## Próximas etapas necessárias para alcançar a solicitação integral

1. Suportar VirtIO-blk moderno, AHCI e NVMe como backends do mesmo
   contrato de bloco, com IRQ/MSI-X, buffers de DMA e limites físicos.
2. Introduzir um driver manager com binding PCI/ACPI/INF por Hardware ID,
   lifecycle PnP, MMIO, DMA, interrupções, hotplug, energia e falha segura.
3. VFS e FAT inicialmente readonly sobre os blocos reais; depois
   escrita, flush, recuperação após desligamento e permissões.
4. Integrar processos CPL3 ao scheduler: salvar frames de traps, alternar
   tarefas na interrupção do timer, TSS/RSP0, prioridades, waits e teardown;
   duplicar handles apenas por política explícita de transferência.
5. Sessions: credenciais, UID/GID/tokens, ACL, namespace de objetos, job
   objects, isolamento de recursos, autenticação e lifecycle de serviços;
   ainda não foi implementado e não existe um login simulado.
6. Para um SYS Windows real, selecionar primeiro um miniport com direitos
   de uso e versão documentados, resolver suas importações NT/Storport
   conforme evidências REA e verificar DriverEntry/AddDevice/PnP/IO/Remove.
   Somente após testes reais com múltiplos dispositivos habilitar
   descoberta automática. NDIS requer sua própria camada de tradução.

## Repetição dos testes

Na raiz Winux, execute:

    & "C:\Program Files\Python310\python.exe" scripts/build/make_block_preview.py
    & "C:\Program Files\Python310\python.exe" tests/runners/test_console_wrapper_fls_candidate.py --iso build/block-preview/winux-0.38-wrapper-fls-candidate.iso --result-tag console-block-p4 --block-image build/block-preview/driver-test-disk.img

    & "C:\Program Files\Python310\python.exe" scripts/build/make_proc_space_preview.py --experiment scheduler
    & "C:\Program Files\Python310\python.exe" tests/runners/test_console_wrapper_fls_candidate.py --iso build/scheduler-preview/winux-0.38-wrapper-fls-candidate.iso --result-tag console-scheduler-resources-p4 --check-scheduler --check-proc-space

O gerador de disco (make_block_fixture.py) deve ser executado somente se a
fixture ainda não existir, pois recusa substituí-la. QEMU não possui rede
no teste; as imagens 0.38 e P4 anteriores permanecem intactas.
