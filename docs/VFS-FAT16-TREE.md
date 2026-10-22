# Winux FAT16 — diretórios e cadeias de clusters

Data: 21/10/2026. Somente a candidata Foundation Preview/QEMU.

## Implementação no VFS existente

src/fs/fat16_ro.h continua sendo o único backend FAT16, consumindo
wx_block_read no controlador PCI/VirtIO existente. src/fs/vfs.h aumenta a
capacidade de leitura controlada para 8192 bytes, acrescenta atributos
de entrada e permite listar um diretório selecionado.

O parser agora aceita até três componentes no caminho 8.3 (raiz, pasta e
subpasta), valida cada componente, rejeita nomes vazios, "..", extensões
inválidas e caminhos acima do limite. O backend atravessa diretórios
FAT16 reais, valida as cadeias e verifica o limite de clusters antes de
ler. Um arquivo com cadeia circular é rejeitado; o laço nunca segue
indefinidamente.

O volume permanece **somente leitura**. Não foram implementados
criação/exclusão, alteração de metadados, renomeação, arquivos longos,
gravação, flush, journaling ou cache de páginas de sistema de arquivos.

## Imagens preservadas e nova fixture

Os arquivos de teste anteriores permanecem intactos:

- build/vfs-preview/fat16-test-disk.img
- build/block-preview/driver-test-disk.img

A nova imagem descartável está em:

    build/vfs-preview/fat16-tree-disk.img

SHA-256 esperado:
f88540f059eab658ab51d02e313a3847dd14d55f3c9184a7aa6ed190ba6748da

O gerador scripts/build/make_fat16_tree_fixture.py cria o novo arquivo
somente se ele ainda não existir, validando o SHA da imagem-base e
recusando sobrescrever arquivos.

A fixture contém:

- HELLO.TXT, 900 bytes, clusters 2 -> 4
- STATUS.TXT, 28 bytes
- LOGS/REPORT.TXT, 6144 bytes, 12 clusters não contíguos
- LOGS/ARCHIVE/NOTES.TXT, 1536 bytes, três clusters não contíguos
- LOGS/LOOP.TXT, cadeia malformada 23 -> 24 -> 23, propositalmente
  rejeitada pelo leitor de arquivos.

O QEMU apresenta essa imagem como readonly; a ISO 0.38/P4 histórica não é
substituída nem reescrita.

## Testes e comandos

Dentro do Winux experimental:

    fs-test
    fs-tree-test
    lsdisk
    lsdisk LOGS
    lsdisk LOGS/ARCHIVE
    catdisk LOGS/REPORT.TXT
    catdisk LOGS/ARCHIVE/NOTES.TXT

O teste fs-tree-test valida todos os bytes dos arquivos de 6144 e 1536
bytes, as cadeias não contíguas, rejeição de loops e de traversal.
O harness também verifica os diretórios pelas linhas VFS_FILE/VFS_DIR,
e espera erro para LOOP.TXT e caminhos inválidos.

Para repetir:

    .\scripts\build\verify_foundations.ps1

As evidências ficam em build/console-foundations-p4-test.json e no log
serial. O script confere antes e depois os hashes das imagens de disco
e ISOs antigas.

## Próximo requisito antes de escrita

Generalizar FAT16 para tamanhos maiores sem armazenar todo o arquivo em
um buffer fixo: handles de arquivo por processo, leitura parcial por
offset, cache de setores e invalidação de dispositivo. Depois disso,
implementação de escrita com operações ordenadas, flush, reversão após
falhas, testes de corte de energia em volumes descartáveis, limites de
capacidade, permissões e isolamento do processo.

Jamais ativar a escrita em volumes físicos do usuário neste estágio.
