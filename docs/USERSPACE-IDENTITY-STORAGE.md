# Winux: Ring 3, identidade e armazenamento persistente

Data: 2026-10-21. Estado: **protótipo isolado e testável**, não uma nova ISO estável.
Fontes do experimento: `experiments/ring3/boot.c`, `src/arch/x86_64/ring3_preview.h`,
`src/storage/uefi_install_preview.h`, `abi/native_services_v1.h`.

## 1. Privilégios de CPU versus contas de usuário

- **Ring 0/CPL0:** código do kernel, drivers e operações privilegiadas. Não é o perfil administrativo de um usuário.
- **Ring 3/CPL3:** código sem privilégio, incluindo executáveis Windows PE, futuros executáveis ELF e serviços da sessão.
- **UID/GID/capacidades:** conceito lógico independente de CPL. Administradores normais também executam em Ring 3; solicitações privilegiadas devem atravessar uma autorização explícita no kernel ou num serviço confiável.
- **Rings 1 e 2:** não necessários no desenho x86-64; não devem ser inventados como contas ou tiers de permissão.

### O que o preview Ring 3 realmente prova

A compilação experimental instala descritores de usuário e TSS/RSP0, mantém as regiões do kernel somente para supervisor, mapeia duas páginas de usuário (código RX, pilha RW/NX) e habilita `int 0x80` com DPL3. Uma função de usuário escreve na tela via syscall e termina com status 42. Uma leitura de memória supervisor é rejeitada pelo hardware com #PF; a tentativa de acesso a uma porta I/O é rejeitada por #GP. Ambos os traps são recuperados em uma sessão de teste controlada, e o shell Ring 0 permanece vivo.

A ABI de teste **Winux native v1** contém somente as duas chamadas reais, `WX_NATIVE_EXIT` e `WX_NATIVE_CONSOLE_WRITE`; não reutiliza os números NT da Microsoft de `abi/nt_services_19041.json`. **Não é um processo geral**: não existem CR3 por processo, escalonador, exec ELF/PE Ring 3, handles por sessão ou controle completo de teardown. A rota atual de DLLs Windows continua em CPL0 apenas para fixtures explicitamente confiáveis.

## 2. Armazenamento e instalação de configuração

O experimento anexa **um arquivo de disco FAT16 dedicado à VM** contendo a assinatura exata `WINUX-DATA-V1` em `WINUX.ID`. Durante o boot e **antes de ExitBootServices**, o instalador utiliza o driver FAT do firmware UEFI para localizar apenas essa mídia e criar diretórios ausentes. Não formata discos. Em volumes sem a assinatura ou no CD de boot, não escreve.

Estrutura criada no volume:

```text
/SYSTEM/CONFIG/SYSTEM.INI       # Versão, formato, setup=required
/SYSTEM/CONFIG/USERS.INI        # count=0, setup=required
/SYSTEM/DRIVERS/
/SYSTEM/LIBS/WIN32/
/SYSTEM/LIBS/ELF/
/USERS/
/APPS/
/VAR/LOG/
/VAR/CACHE/
/TMP/
/WINUX.ID                      # assinatura do volume de dados
```

Na segunda inicialização, arquivos existentes não são sobrescritos e o firmware retorna `WINUX_STORAGE: EXISTING`. A verificação FAT lê setores e diretórios reais da imagem de disco, incluindo conteúdo dos dois INI.

**Limite arquitetural:** o kernel ainda não possui driver de bloco ou VFS após ExitBootServices. Portanto, pastas persistem de fato no volume, mas edição/criação de usuários durante uma sessão Winux ainda NÃO está disponível. O arquivo de contas define explicitamente `count=0`; não existe login ativo, conta administradora sem senha nem autenticação simulada.

Não usar a assinatura de volume como mecanismo de autenticação. Ela é apenas um bloqueio de segurança contra escrita acidental em discos errados; um volume arbitrário malicioso pode imitá-la. Não habilitar auto-instalação ou formatação de discos reais sem detecção explícita de hardware, escolha do destino, confirmação, backup e testes.

## 3. Como reproduzir os experimentos em QEMU

A partir da raiz do repositório:

```powershell
$py = "C:\Program Files\Python310\python.exe"
& $py scripts/build/create_winux_data_image.py  # APENAS se arquivo ainda não existir
& $py scripts/build/make_ring3_preview.py
& $py tests/runners/test_ring3_preview.py --iso build/ring3-preview/winux-0.38-wrapper-fls-candidate.iso --data-image build/ring3-preview/winux-data.img --expect-existing --result-tag ring3-preview-verification
& $py tests/checks/check_winux_data_image.py
```

O gerador de disco **recusa sobrescrever arquivos existentes**. O empacotador gera somente `build/ring3-preview/`, preservando imagens P4 e 0.38. QEMU é configurado sem rede, TCG, 1 CPU e volume adicional `if=virtio`.

## 4. Caminho para o sistema de usuários completo

1. **Driver de bloco + montagem após UEFI:** implementar AHCI/NVMe/Virtio-blk conforme os dispositivos suportados, com operações setoriais verificadas, VFS e montagem persistente. A fase UEFI serve somente à primeira instalação.
2. **Identidade/autenticação:** armazenar credenciais com KDF moderna e sal aleatório individual, limite de tentativas, bootstrap que exija senha de administrador criada pelo usuário, verificação em tempo constante, recuperação e proteção de arquivos. Não permitir usuários implicitamente administradores. Proteger dados sensíveis no disco.
3. **Sessões e acesso:** UIDs/GIDs/capabilities, ACL, isolamento de home, ownership, handles por processo, política de privilégios e credenciais por sessão. CLI do kernel não pode ser shell geral de um usuário autenticado.
4. **Processos Ring 3:** espaços de endereçamento separados, switch de CR3, TSS RSP0 por CPU, signal/exception handling, escalonador e IPC. Só liberar execução de binários não confiáveis após isolamento.
5. **Compatibilidade Windows:** DLLs próprias ntdll/kernel32/msvcrt como adaptadores sobre o kernel Winux; REA continua validando contratos por hash de binário. CMD e CLR continuam marcos separados.
6. **Linux futuro:** adicionar carregador ELF64, ambiente POSIX e tradutor de syscalls Linux por versão e arquitetura **sobre os mesmos processos, VFS, handles e controle de acesso**. Não construir um segundo kernel, scheduler ou sistema de usuários; não confundir números Linux, NT e Winux.

## 5. Limites e critérios de promoção

Este experimento ainda não passou VirtualBox, Proxmox, hardware físico, stress multicore nem revisão de segurança. Ele não deve ser promovido automaticamente à ISO estável. A qualidade é determinada por dados persistentes que sobrevivem ao reboot, rejeição de discos não identificados, proteção CPL3 comprovada por fault e regressões existentes em QEMU.
