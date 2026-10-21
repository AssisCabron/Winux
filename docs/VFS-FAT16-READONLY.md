# Winux — FAT16 somente leitura sobre o dispositivo de blocos PCI

Data: 21/10/2026. Candidata experimental P4/foundation.
Os arquivos originais da ISO 0.38 e da P4 histórica permanecem intactos.

## Implementação

- src/fs/vfs.h: uma única interface de volume montado, lookup, read e list.
  Não há segunda implementação do controlador de hardware ou do heap.
- src/fs/fat16_ro.h: backend FAT16 que consome wx_block_read de
  src/storage/block_core.h; valida BPB, limites do disco e contagem de
  clusters, percorre a FAT16 em leitura e carrega cadeias não contíguas.
- src/storage/virtio_blk_legacy.h: segue sendo o único controlador usado,
  com PnP PCI, DMA registrado, reset/rebind e capacidade observada.
- scripts/build/make_fat16_fixture.py: gera uma única imagem FAT16
  descartável de 16 MiB. Recusa sobrescrever o arquivo existente.
- tests/runners/test_console_wrapper_fls_candidate.py --check-vfs:
  comprova montagem, enumeração, leitura e rejeição de caminhos inválidos.

O volume possui duas entradas 8.3 no diretório raiz:

1. HELLO.TXT: 900 bytes, cluster 2 -> cluster 4 -> EOF. Comprova acesso
   por FAT e setores não contíguos, não apenas uma leitura direta do LBA.
2. STATUS.TXT: 28 bytes contendo WINUX_FAT16_RUNTIME_READ_OK.

Para permitir regressão PCI/DMA com o mesmo disco, o setor 16, que pertence
à área reservada pela formatação de teste, contém um marcador. Nunca
interpretar esta fixture como uma imagem FAT genérica de usuário.

## Comandos no terminal da ISO experimental

    fs-test
    lsdisk
    catdisk STATUS.TXT
    catdisk HELLO.TXT

Os comandos retornam dados do bloco montado depois de ExitBootServices.
Não usam a RAMFS de arquivos incluídos no boot. Nomes inválidos, tentativa
de traversal e arquivos inexistentes retornam VFS_NOT_FOUND.

## Restrições

Este estágio suporta um volume FAT16 somente leitura, formato 8.3 no
diretório raiz, arquivos de até 2048 bytes e setores de 512 bytes.
Não há subdiretórios, long file names, arquivos grandes, cache,
gravação, criação, renomeação, remoção, flush, journaling ou recovery.
Montagem de volumes USB/SATA/NVMe físicos e dados pessoais está fora de
escopo. O disco de 16 MiB é sempre apresentado como readonly pelo QEMU.

Requisitos para habilitar gravação: política de ownership por processo,
I/O assíncrono, cache e flush consistentes, atualização atômica da FAT
e diretórios, detecção de mídia, sistema de permissões e testes de
desligamento brusco em discos descartáveis.

## Testes

Comando único de regressão de armazenamento, processos e serviços:

    .\scripts\build\verify_foundations.ps1 -Python "C:\Program Files\Python310\python.exe"

Também é possível repetir apenas o teste de disco:

    & "C:\Program Files\Python310\python.exe" tests/runners/test_console_wrapper_fls_candidate.py --iso build/foundation-preview/winux-0.38-wrapper-fls-candidate.iso --result-tag console-fat16-vfs --block-image build/vfs-preview/fat16-test-disk.img --check-vfs --check-proc-space --check-scheduler --check-user-scheduler --check-services

Artefatos:

- build/vfs-preview/fat16-test-disk.img
- build/vfs-preview/fat16-fixture.json (geometria e hashes por arquivo)
- build/console-foundations-p4-test.json
- build/console-foundations-p4-serial.log

O teste integrado final passou em 47 grupos QEMU. Uma verificação adicional
foi incluída para desmontar o controlador PCI, invalidar a montagem VFS,
rejeitar leituras durante a ausência do dispositivo e montar novamente.
Os resultados da execução mais recente constam em
build/console-foundations-p4-test.json e no log serial. Isso não implica compatibilidade com
todos os sistemas de arquivos FAT16 nem segurança de escrita.
