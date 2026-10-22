# USER-PE64 — primeiro executável PE32+ externo em CPL3

Data: 22/10/2026. Escopo: kernel x86-64 original, UEFI/QEMU Q35/TCG, um núcleo, imagem FAT16 descartável e somente leitura. **Este não é um carregador geral Win32.**

## Integração única, sem duplicar componentes

Comando de demonstração: `exec APPS/USER64.EXE`.

1. O shell resolve o arquivo pelo **VFS existente**, montado a partir do bloco PCI VirtIO existente e do FAT16 somente leitura existente.
2. `src/process/user_pe_loader.h` valida assinatura DOS/PE, arquitetura AMD64 PE32+, número de seções, base/entrada, alinhamento, diretórios de dados vazios, ausência de imports/relocations/TLS/CLR e tamanho limitado. Exige uma identidade FNV-1a64 fixada no build da fixture.
3. O **Process Manager existente** cria PID, objetos/handles, quadros físicos, CR3 e contexto de thread. O carregador não cria uma segunda tabela de processos.
4. Cabeçalhos PE são copiados para uma página **RO/NX**, seção de código para uma página **RX**, pilha para uma página **RW/NX**. Como a base PE fica imediatamente antes de uma fronteira de 2 MiB, a página de cabeçalho tem **sua própria PT no PDE anterior** — nunca é escrita na PT de código no índice 511.
5. `wx_proc_run_entry` entra no ponto de entrada de arquivo em CPL3 usando TSS RSP0, CR3 e thread do próprio processo. A fixture lê a assinatura **MZ da página de cabeçalho de dentro de CPL3**, escreve mensagem via `WX_NATIVE_CONSOLE_WRITE` e termina por `WX_NATIVE_EXIT`.
6. `wx_proc_reap` libera cabeçalho, PT de cabeçalho, código, pilha, tabelas de página e handles, verificando o número de quadros livres antes/depois.

A implementação **não** usa o carregador `pe_load` de fixtures CPL0 como caminho de execução. Ele permanece apenas como prova histórica de contratos Win32.

## Limites deliberados

- PE32+ AMD64 de 1024 bytes, uma única seção `.text`, image base fixo `0x10EFFF000`, entry RVA `0x1000`, sem relocations/imports/TLS/DLL/CLR. Os executáveis são fixtures independentes do kernel; o layout e a integridade são verificados antes de criar PID.
- O controle FNV-1a64 é **não criptográfico** e não autentica código de terceiros. O FAT16 de teste é mantido somente leitura; não carregar EXEs não confiáveis ou ampliar a allowlist sem política de assinatura e carregador robusto.
- Não há memória dinâmica geral de usuário, mais de uma seção PE, ASLR, loader de DLLs em CPL3, parâmetros PEB/TEB reais para aplicativos externos ou CRT/CMD. Há uma rotina de execução direta do Process Manager, mas não um scheduler geral de aplicativos PE.
- Não existem drivers Windows SYS autorizados, acesso a discos físicos, USB, hotplug nem garantias SMP. Os testes QEMU não certificam comportamento em computadores físicos.

## Artefatos de teste reproduzíveis

O gerador inicial de fixtures `scripts/build/make_user_pe_fixture.py` cria `user-pe-fat16.img` a partir da imagem FAT16 árvore, **sem alterar a imagem ancestral**. `scripts/build/make_user_pe_v2_fixture.py` cria a **segunda revisão imutável** com acesso real do código usuário à página DOS `MZ`; também cria uma cópia deliberadamente adulterada para o teste de integridade.

Arquivos atuais:
- `build/user-pe-preview/user-pe-fat16-v2.img`
- `build/user-pe-preview/user-pe-tampered-fat16-v2.img`
- `build/user-pe-preview/user-pe-fixture-v2.json`
- `build/user-pe-preview/user-pe-allowlist.h` — gerado a partir dos hashes dos arquivos de fixture.
- `build/user-pe-preview/winux-0.38-wrapper-fls-candidate.iso`

Em um checkout novo, gerar as fixtures uma unica vez com python scripts/build/make_user_pe_fixture.py e depois python scripts/build/make_user_pe_v2_fixture.py (ambos recusam sobrescrever imagens existentes). Repetir os testes com `.\scripts\build\verify_user_pe.ps1`. O script verifica hashes dos arquivos e das ISOs estáveis, compila somente o preview, executa a regressão de carga/falhas, verifica a cópia adulterada e executa a suíte de integração, sem modificar imagens existentes.

## Provas esperadas no QEMU

- `USER_PE_EXEC_OK: external PE32+ on CPL3 via FAT16` — mensagem **em código PE externo**.
- `PROCESS_CREATED pid=...`, `PROCESS_EXIT code=0 fault=0`, `PROCESS_REAP_OK` e `USER_PE_LIFECYCLE_OK` — Process Manager real e restauração de quadros/handles.
- PE malformado e caminho desconhecido recusados **antes de criar PID**.
- `WRITE64.EXE` escreve na própria seção RX → #PF (vector=14, erro de Write/User/Present); `NX64.EXE` tenta executar a pilha → #PF (Present/User/Instruction).
- Cópia FAT16 alterada de `USER64.EXE` rejeitada por divergência de conteúdo e outro processo legítimo continua executável.
- `build/console-user-pe-foundation-v2-test.json`: regressão integrada com **67 grupos aprovados** na versão testada. Os resultados posteriores de recompilação devem ser verificados novamente pelo hash de ISO.

## Depois deste marco

O próximo passo passa a ser um loader de múltiplas seções PE com alocação de páginas por região, relocations permitidas e um contrato claro entre o carregamento da imagem e a criação do processo. **Não habilitar CMD.exe ou execução arbitrária ainda.**
