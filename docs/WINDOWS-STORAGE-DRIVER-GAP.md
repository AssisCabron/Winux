# Winux — dependências reais de drivers Windows de armazenamento

Data: 21/10/2026. Inventário **estático**, obtido dos SYS locais.
Nenhum SYS foi executado, copiado para a ISO, alterado ou redistribuído.

## Proveniência

Gerador: scripts/compatibility/inventory_windows_drivers.py.
Inventário JSON com todas as importações: build/driver-compat-imports.json.

| Binário Windows local | SHA-256 | Imports únicos | Dependências |
|---|---|---:|---|
| stornvme.sys | 8867e421b3a4008ca54230da64c6a6eaa5551775800f384c07a647ebf6b274d3 | 22 | ntoskrnl.exe, storport.sys |
| storport.sys | 8615e657d973311d7cf77996b80dc17d0cedce031afe1d2350f5214871b28b72 | 270 | hal.dll, ntoskrnl.exe |
| disk.sys | 1029ebc39859fe68459214b2e35c970b92e7eb367e46fb536f46f26b2a5c8f8b | 99 | classpnp.sys, ntoskrnl.exe |
| partmgr.sys | 0c5130fc38eaaf85e768f3269b635d79780b13c00690838d7afe6fefb4cfc5e7 | 139 | hal.dll, ntoskrnl.exe, wmilib.sys |
| pci.sys | b41ebcce9f68256e869cee6c9a122d0c495d8b8f9095a5a4ed13d034d051515a | 254 | ntoskrnl.exe, hal.dll, pshed.dll, wpprecorder.sys, API-set |

O perfil acima é da versão instalada no computador analisado. Alterações do
Windows podem mudar hashes, ordinais, ABI e semântica, exigindo novo dossiê REA.

## Miniport de referência: stornvme.sys

Foram identificadas 18 importações diretas de Storport:

- StorPortInitialize, StorPortExtendedFunction, StorPortGetUncachedExtension
- StorPortGetDeviceBase, StorPortGetBusData, StorPortGetPhysicalAddress
- StorPortGetVirtualAddress, StorPortGetScatterGatherList
- StorPortNotification, StorPortPause, StorPortResume
- StorPortSetDeviceQueueDepth, StorPortStallExecution
- StorPortRegistryRead, StorPortAllocateRegistryBuffer,
  StorPortFreeRegistryBuffer, StorPortQuerySystemTime, StorPortDebugPrint.

Quatro importações adicionais pertencem a ntoskrnl.exe, todas ligadas à
consulta/registro de configurações dinâmicas (RtlQueryFeatureConfiguration,
RtlQueryFeatureConfigurationChangeStamp,
RtlRegisterFeatureConfigurationChangeNotification e
RtlUnregisterFeatureConfigurationChangeNotification).

**Importante:** 22 nomes não implicam implementação de apenas 22 funções.
StorPortExtendedFunction multiplexa diversas operações com contratos,
estruturas, efeitos e regras de IRQL diferentes. StorPortInitialize precisa
de adaptador, callbacks, extensões e lifecycle completo. A enumeração de
imports não inclui chamadas indiretas, contratos de hardware NVMe, PnP,
IRP, filas, SRB, power, cancelamento ou falhas de dispositivo.

## Decisão de arquitetura

A infraestrutura nativa de hardware da ISO experimental agora utiliza
wx_pci_function, wx_dma_region, wx_block_device e o driver VirtIO-blk
através dessas interfaces já existentes. NÃO criar um segundo PCI, segundo
escalonador, segundo heap ou outro VFS para emular Windows.

Um adaptador Storport de compatibilidade no futuro deve traduzir seu modelo
para essas primitivas compartilhadas. A primeira execução SYS deve usar
um driver com licença autorizada, versão identificada, hardware emulado
e contrato diferencial verificado pelo REA. Todas as funções ainda não
implementadas devem falhar explicitamente, sem falsos retornos SUCCESS.

Não utilizar esta imagem experimental com SYS desconhecidos. O código de
drivers roda em ring 0 e não existe IOMMU por dispositivo; uma DLL/SYS maliciosa
pode corromper o kernel e realizar DMA fora dos buffers registrados.

## Critérios para execução real de um miniport

1. Import loader de SYS x64 com relocação, características PE de driver
   e tratamento de erros, sem invocar DriverEntry prematuramente.
2. Modelo mínimo real de DriverObject, DeviceObject, PnP e power;
   dispositivo PCI devidamente associado ao driver por Hardware ID.
3. Memória física/virtual e DMA coerente, tradução bus/VA, scatter-gather,
   áreas comuns, MDLs e IOMMU quando necessário.
4. IRQL, spinlocks, IRQ/MSI-X, DPC, timers, SRB, completion, cancelamento,
   timeouts e sequência exata das callbacks de Storport.
5. File/volume device e integração com o VFS, sem duplicar dispositivos de
   bloco já existentes.
6. Testes QEMU com leitura de setor real, falha, detach, reinit, power,
   rejeição de buffers e nenhuma corrupção de disco.
7. Registrar os contratos observados pela engenharia reversa (hashes,
   estruturas, offsets, condições de falha), não copiar implementação
   proprietária ou considerar presença de export como funcionalidade.

## Próxima investigação REA direcionada

Investigar a rota de StorPortInitialize e StorPortExtendedFunction para
o stornvme.sys de hash fixado, incluindo callbacks, ABI de estruturas
HW_INITIALIZATION_DATA, mapeamento/registro de dispositivos e
primeiro fluxo de SRB. Isso define o primeiro lote significativo de
implementação; não adianta publicar stubs com o mesmo nome.

Para reproduzir somente a coleta (leitura de PE, nenhum SYS carregado):

    & "C:\Program Files\Python310\python.exe" scripts/compatibility/inventory_windows_drivers.py

