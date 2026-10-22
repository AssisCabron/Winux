# Winux 0.38 - kernel experimental original

ISO estavel atual: `build/winux-0.38-x64.iso`, SHA-256 `700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0`.

**Branch experimental Ring 3 e armazenamento UEFI:** [docs/USERSPACE-IDENTITY-STORAGE.md](docs/USERSPACE-IDENTITY-STORAGE.md). O prototipo usa codigo CPL3 real, gate de syscall nativo, falhas de acesso a paginas supervisor e I/O privilegiado com recuperacao, e cria SYSTEM/CONFIG, USERS e demais diretórios num disco FAT16 exclusivamente marcado WINUX-DATA-V1. A verificacao de reboot preserva os arquivos; volumes sem assinatura nao sao modificados. A autenticacao de contas, filesystem persistente em runtime, scheduler preemptivo e sessoes reais ainda NAO existem na ISO estavel. As provas separadas de CPL3 cooperativo e bloco PCI estao documentadas abaixo.

**ORBIS OS — GUI de verdade no QEMU:** [docs/ORBIS-GUI-RENDER-IRQ12.md](docs/ORBIS-GUI-RENDER-IRQ12.md). O preview isolado `--experiment integration --orbis-gui` usa framebuffer UEFI, fonte Segoe UI rasterizada e proporcional (8 bpp), wallpaper fotográfico RGB888 1280x800 embutido via COFF, janelas e menus experimentais e mouse PS/2 por IRQ12 com cursor incremental. Os testes gráficos QEMU incluem boot, criação de conta de demonstração em RAM, login, logout, guest e clique físico em Files. Para abrir uma janela GTK visível: `python scripts/build/run_orbis_gui_visible.py`. **Não é autenticação segura, não há VFS gráfico real, a GUI não foi integrada à ISO estável.**

**USER-PE64 (execução de PE externo em CPL3):** [docs/USER-PE64-CPL3-VFS.md](docs/USER-PE64-CPL3-VFS.md). O preview separado `build/user-pe-preview/` lê `APPS/USER64.EXE` de um disco FAT16 somente leitura por meio do VFS existente, valida uma fixture PE32+ sem imports/relocações, mapeia cabeçalhos R/NX, código RX e pilha RW/NX, cria um processo no Process Manager existente, entra em CPL3, escreve pelo gate INT80, sai e recupera quadros/handles. O teste QEMU aprovado inclui página de cabeçalho atravessando fronteira de 2 MiB, falhas W^X/NX e rejeição de imagem adulterada. **Não é loader de Win32 genérico e não modifica a ISO estável.** Reproduzir: `.\scripts\build\verify_user_pe.ps1`.

**VT-d de hardware (preview QEMU separado):** [docs/INTEL-VTD-SECOND-LEVEL-DMA.md](docs/INTEL-VTD-SECOND-LEVEL-DMA.md). Build com `--experiment integration --vtd` em `build/vtd-preview/`, sem modificar a ISO 0.38 estável. Lê DMAR ACPI real e escopos BDF, habilita domínios Intel VT-d, negocia VirtIO PCI moderno com ACCESS_PLATFORM, restringe páginas DMA e verifica faults e invalidação da IOTLB em QEMU. Não é um driver/gerenciador IOMMU geral: só um controlador/segmento e um dono DMA ativo; não usar PCI não confiável em hardware real.

**Isolamento CPL3 e DMA dedicado:** [docs/CPU-ISOLATION-DMA-BOUNDARY.md](docs/CPU-ISOLATION-DMA-BOUNDARY.md). A Foundation Preview usa CR3 e permissões User/Supervisor para bloquear acessos entre processos e à região DMA; os buffers do VirtIO vêm de um pool coerente de 32 KiB com dono/direção/limites, revogação e limpeza no detach. Validado em QEMU; **sem IOMMU**, portanto não isola um dispositivo PCI hostil.

**FAT16 com subdiretórios (somente leitura):** [docs/VFS-FAT16-TREE.md](docs/VFS-FAT16-TREE.md). O kernel experimental monta um volume FAT16 pelo driver VirtIO PCI, percorre caminhos 8.3 de até três componentes, lê arquivos de até 8192 bytes e rejeita cadeias FAT cíclicas. Comandos: `fs-test`, `fs-tree-test`, `lsdisk LOGS`, `catdisk LOGS/REPORT.TXT`. Ainda sem gravação, cache ou suporte a arquivos maiores que 8 KiB.

**Process Manager e preempção real CPL3:** [docs/PROCESS-MANAGER-PIT-PREEMPTION.md](docs/PROCESS-MANAGER-PIT-PREEMPTION.md). Processos de teste com PIDs únicos, handles próprios e liberação integral das páginas; o PIT/IRQ0 interrompe dois loops CPL3 sem YIELD, alterna CR3/RSP0 e conserva RFLAGS/FPU. A regressão foundation experimental passou 65 grupos; os contextos FPU/registradores e as pilhas RSP0 da preempção agora pertencem diretamente aos processos gerenciados. Ainda não é um escalonador preemptivo geral em produção.

**IPC bloqueante CPL3:** [docs/CPL3-BLOCKING-IPC.md](docs/CPL3-BLOCKING-IPC.md). A syscall nativa RECEIVE_WAIT suspende memd quando a mailbox está vazia; clockd o desperta por mensagem ou o PIT encerra a espera por timeout, sem polling e sem fila duplicada.

**Threads CPL3 e suspensão por timer:** [docs/PROCESS-THREAD-LIFECYCLE.md](docs/PROCESS-THREAD-LIFECYCLE.md). Duas threads do mesmo processo compartilham CR3/código/handles, mas usam pilhas, TIDs e contextos separados. `SLEEP_TICKS` usa BLOCKED/READY do Process Manager.

**Inspeção de threads:** `ps` agora diferencia serviço ativo de estado de agendamento, exibindo `sched_state=BLOCKED/READY/RUNNING` e TID; `threads` enumera threads reais do Process Manager, sem processos fictícios.

**Processos CPL3 residentes (clockd e memd):** [docs/BACKGROUND-SERVICES.md](docs/BACKGROUND-SERVICES.md). Na ISO experimental, clockd consulta ticks PIT e memd consulta as paginas livres, ambos por syscalls reais e CR3 separados. Trocam amostras por uma mailbox IPC limitada; uma falha CPL3 do memd é isolada e o supervisor o reinicia. Aparecem em `ps` e `services`, com parada e reinicio por `svc`. Nao e ainda um Windows Service Control Manager completo; o IRQ0 persistente existe apenas na ISO foundation experimental.

**Armazenamento Windows (análise estática do Storport):** [docs/WINDOWS-STORAGE-DRIVER-GAP.md](docs/WINDOWS-STORAGE-DRIVER-GAP.md). Inventário dos imports de stornvme.sys, storport.sys, disk.sys, partmgr.sys e pci.sys, identificados pelos hashes locais. Nenhum driver SYS foi executado ou copiado para a ISO.

**Correcoes de ABI, RFLAGS, CPL3 reagendavel e PCI/DMA:** [docs/CPL3-SCHEDULER-PCI-PNP-DMA.md](docs/CPL3-SCHEDULER-PCI-PNP-DMA.md). A variante experimental usa uma rotina context_switch.S, captura o frame de INT 80h para suspender/retomar dois processos CPL3, valida MXCSR e IF, reutiliza o gerenciador NT de handles e descobre/binda o VirtIO via PCI/PnP com buffers DMA registrados. O IRQ0 fica instalado durante toda a sessão da candidata foundation, mas o kernel estável 0.38 continua sem preempção, IOMMU ou execução de drivers SYS Windows.

**Experimento integrado de armazenamento, CR3 e scheduler:** [docs/STORAGE-PROCESS-ARCHITECTURE.md](docs/STORAGE-PROCESS-ARCHITECTURE.md). A ISO experimental no QEMU passou 65 grupos, incluindo leitura real de um disco VirtIO PCI após UEFI, espaços de endereçamento CPL3 separados, troca cooperativa de duas tarefas e handles NT por tarefa. Nao e driver Windows SYS, nem processo/servico completo. Repetir: `./scripts/build/verify_foundations.ps1`.

**Teste da DLL Windows propria:** [docs/WINDOWS-DLL-COMPATIBILITY.md](docs/WINDOWS-DLL-COMPATIBILITY.md). A mesma winuxplugin.dll e o mesmo plugintest.exe passaram no Windows e no Winux P4 (QEMU, 39 verificacoes). O teste e restrito a uma fixture confiavel em CPL0; nao habilita DLLs arbitrarias.

**Estrutura atual do codigo, subsistemas, testes e builds:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). As variantes P1-P4 estao em `experiments/candidates/`, scripts de compilacao em `scripts/build/`, verificacoes em `tests/runners/` e probes em `tests/probes/`.

**Candidata experimental P4 (21/10/2026):** `build/winux-0.38-wrapper-fls-candidate.iso`, SHA-256 `4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77`. Em QEMU, 37 grupos da regressao passaram: `KernelBase!HeapSetInformation`, alocacao e realocacao originais NTDLL apos politica de heap 2, e wrappers originais KernelBase FLS com callbacks e limpeza do contexto. A suite executa os testes novos duas vezes. O arquivo 0.38 estavel nao foi sobrescrito. P4 ainda nao foi validada no VirtualBox, Proxmox ou hardware e nao executa a CRT Microsoft nem o CMD. [Relatorio P4](HEAP-POLICY-FLS-CANDIDATE-0.38.md).

A 0.38 inclui o HelloWorld.exe fornecido pelo usuario, sem modificar seus 4096 bytes (SHA-256 f2f667e330da9f190eda77c74781963a0495c3953a653747fe475b99421efdda). `run HelloWorld.exe` identifica imagem gerenciada e informa a ausencia do runtime .NET CLR antes da validacao de entrada nativa. O arquivo pede .NET Framework 4; nao foi executado. Regressao QEMU e VirtualBox aprovada; registros em build/helloworld-user-test.json.

A 0.37 publica SystemBasicInformation em KernelBase usando a consulta NT original. HeapCreate/HeapDestroy originais passaram com alocacao de 64 KiB, preservacao entre escopos e restauracao no teardown/rollback. Dois ciclos QEMU e VirtualBox aprovados; ver [evidencias](../windows-analysis/cmd/kernelbase-heap-integration.json). HeapSetInformation, primeiro inicializador CRT e CMD continuam pendentes; 0.36 preservada.

A 0.36 mantem linha de comando ANSI e perfil de versao no contexto nativo, com GetCommandLineA/GetVersionExW originais e limpeza antes do heap. Dois ciclos QEMU e VirtualBox passaram, incluindo falha posterior na abertura do console, rollback completo e nova inicializacao. Veja [evidencia e limites](../windows-analysis/cmd/startup-inputs-persistent.md). CRT attach/CMD ainda nao executam; 0.35 preservada.

A 0.35 valida consumidores originais de startup em conjunto: dez registros ANSI iguais ao Windows, GetVersionExW com perfil sintetico e seletor original, GetStartupInfoW com handles condicionais e limpeza de recursos. Dois ciclos QEMU e VirtualBox aprovados. Comando `nt-startup-test`; veja [contrato e limites](../windows-analysis/cmd/startup-consumers-integration.md). CRT attach e CMD ainda nao executam; ISO 0.34 preservada.

A 0.34 corrige o teste de ambiente para usar o terminador duplo, sem ler padding do heap. Regressao QEMU/VirtualBox aprovada, dois ciclos de ambiente por VM. Dez novas funcoes NTDLL foram analisadas no REA para versao e strings; ver [lote e proximas fronteiras](../windows-analysis/cmd/ntdll-startup-inputs.md). Essas novas rotinas ainda nao foram testadas em guest; CMD continua sem executar. ISO 0.33 preservada.

A 0.33 integra nodes ACP/OEM persistentes ao contexto e testa GetEnvironmentStringsW/FreeEnvironmentStringsW originais com conversao ACP do ambiente completo. Dois ciclos QEMU e VirtualBox passaram; 20 registros de conversao continuam iguais ao Windows. Veja [evidencia e limites](../windows-analysis/cmd/nls-persistent-integration.md). CRT attach e CMD ainda nao executam; 0.32 preservada.

A 0.32 integra console persistente ao contexto nativo: abertura transacional de stdin/stdout/stderr, buffer entre chamadas e fechamento por geracao. Dois ciclos QEMU e VirtualBox passaram, incluindo esgotamento parcial, fechamento antecipado e evento reutilizando slot. Veja [contrato e limites](../windows-analysis/cmd/console-persistent-integration.md). CMD ainda nao executa; ISO 0.31 preservada.

A 0.31 ajusta margens/espacamento do terminal e rejeita encerramento prematuro quando um heap privado ainda esta registrado. Dois ciclos com RtlCreateHeap/RtlDestroyHeap originais passaram no QEMU e VirtualBox, preservando o heap principal ate a liberacao do privado. Veja [evidencia e limites](../windows-analysis/cmd/private-heap-lifecycle.md). CMD ainda nao executa; 0.30 preservada.

A 0.30 testa console com handles tipados publicados no PEB e GetStdHandle/GetFileType/ReadFile/WriteFile originais. Entrada enviada depois do bloqueio passou em dois ciclos por serial no QEMU e por PS/2 no VirtualBox, com leituras parciais, timer/IF e fechamento/restauracao dos handles. Comando `nt-console-test`: digitar `console-input` e Enter. Veja [evidencia e limites](../windows-analysis/cmd/console-native-integration.md). Ainda teste ASCII, sem startup persistente do CMD; 0.29 preservada.

A 0.29 conecta arquivos em memoria aos stubs originais NtReadFile/NtWriteFile/NtQueryVolumeInformationFile e executa ReadFile/WriteFile/GetFileType de KernelBase. Dois ciclos de 14 registros completos iguais ao Windows passaram no QEMU e VirtualBox, mais testes dos wrappers, EOF e fechamento. Comando `nt-file-test`. Veja [evidencia e limites](../windows-analysis/cmd/file-io-integration.md). Console por esses handles, abertura por caminho e CMD ainda pendentes; 0.28 preservada.

A 0.28 implementa MemoryImageInformation (classe 6) e executa RtlPcToFileHeader original sobre as imagens carregadas. Dois ciclos de 27 registros comparados com Windows passaram no QEMU e VirtualBox, com normalizacao explicita dos flags de politica. Comando `nt-image-test`. Veja [evidencia e limites](../windows-analysis/cmd/image-information-integration.md). CMD ainda nao inicia; 0.27 preservada.

A 0.27 executa as APIs originais GetCPInfo, IsValidCodePage e WideCharToMultiByte de KernelBase com nodes ACP/OEM inicializados no heap Microsoft. Vinte registros iguais ao Windows por ciclo, dois ciclos QEMU e dois VirtualBox; aliases e limpeza de recursos verificados. Comando `kb-codepage-test`. Veja [evidencia e limites](../windows-analysis/cmd/kernelbase-nls-integration.md). Nao executa attach completo nem CMD; ISO 0.26 preservada.

A 0.26 integra FLS original: 32 indices, leitura/escrita, callback e erros, com 11 registros iguais ao Windows em dois ciclos QEMU. O encerramento executa RtlProcessFlsData original antes de destruir o heap e verifica callback pendente, TEB e lista limpos e recursos recuperados. Os mesmos dois ciclos passaram no VirtualBox. Veja [contrato e evidencia](../windows-analysis/cmd/fls-lifecycle-integration.md). ISO 0.25 preservada; CRT/CMD ainda nao inicializam.

A 0.25 introduz contexto nativo reutilizavel: heap publicado permanece entre chamadas, e pe_load/runtime_reset exigem limpeza antes de remapear ou zerar o PEB. Dois ciclos, preservacao de dados, parametros originais e reclamacao de alocacao viva passaram no QEMU. Os mesmos ciclos passaram no VirtualBox. Ver [ciclo de vida e limites](../windows-analysis/cmd/native-context-lifecycle.md). CMD continua bloqueado; a abertura do contexto ainda e explicita pelo teste. ISO 0.24 preservada.

A 0.24 publica temporariamente o heap no PEB e executa Create/Normalize/DestroyProcessParameters originais. Dois layouts e tres rejeicoes produzem 23 registros iguais ao Windows, em dois ciclos tanto no QEMU quanto no VirtualBox. Ver [contrato e limites](../windows-analysis/cmd/process-parameters-native-integration.md). A publicacao ainda pertence ao diagnostico; CMD e inicializadores das DLLs permanecem bloqueados. ISO 0.23 preservada.

A etapa 0.23 amplia o heap Microsoft para alocacoes de 0 a 3 MiB e realocacao de 32 para 8192 bytes. Dezenove resultados coincidem com Windows em cada um dos dois ciclos, no QEMU e VirtualBox, com recursos recuperados. Ver [evidencia e limites](../windows-analysis/cmd/heap-growth-integration.md). A ISO 0.22 foi preservada; CMD ainda nao inicia.

A etapa 0.22 executa um ciclo privado do heap Microsoft: Create/Allocate com zero/Size/Free/Destroy, registro e recursos verificados. Dois ciclos passaram na regressao QEMU. Dois ciclos tambem passaram no VirtualBox; a VM Winux ficou executando a 0.22. Ver [integracao e limites](../windows-analysis/cmd/heap-bootstrap-integration.md). A ISO 0.21 foi preservada.

A etapa 0.21 passou na regressao QEMU com locks Microsoft sem contencao e recuperacao dos oito nos do pool estatico de debug. Os 15 registros coincidiram com Windows, normalizando o spin para um processador. Veja [contrato e limites](../windows-analysis/cmd/lock-pool-integration.md). A mesma imagem passou no VirtualBox, incluindo os 15 registros e recuperacao do pool; a VM Winux ficou executando a 0.21. A ISO 0.20 foi preservada; resultados UD2/RO/NX anteriores pertencem aos hashes registrados.

Os testes anteriores de memoria e os 47 resultados de eventos/namespace continuam passando. `nt-object-test` usa namespace de teste; armazenamento guest completo, espera bloqueante e inicializacao do CMD continuam pendentes. Ver [limites e registros](OBJECTS-PROGRESS.md).

Console preto com texto branco, fonte bitmap original com maiusculas e minusculas, cursor, espacamento, rolagem e backspace. A VM Winux do VirtualBox esta usando a 0.38.

## O que funciona

- Boot UEFI x86-64, ExitBootServices e pilha propria de 64 KiB.
- Tabelas de paginas proprias, CR3 instalado, CR0.WP e NX ativos; mapeamento, protecao e remocao de paginas de 4 KiB em janela virtual separada.
- GDT propria de codigo/dados, IDT com excecoes fatais, PIC/PIT a aproximadamente 100 Hz e espera HLT.
- Teclado PS/2 basico US e serial COM1.
- Pool reservado de 2048 paginas (8 MiB), reservas individuais ate 16 MiB em janela virtual de 64 MiB, com verificacao de esgotamento, rollback, reutilizacao e liberacao. Este aumento do orcamento fixo nao demonstra menor consumo que Windows.
- Arquivos somente leitura em RAM e scripts originais .wxs.
- Inspecao PE32+ AMD64 e carregador inicial: mapeamento, imports e relocations DIR64.
- Execucao dos originais hello.exe, runtime.exe, dlltest.exe, dllexit.exe, tlstest.exe e ntmemory.exe, com os mesmos bytes testados no Windows e no QEMU.
- Duas DLLs originais com imports de funcao/dados, ordinais, forwarders e inicializacao em ordem de dependencia. GetModuleHandleW/GetProcAddress consultam modulos ja carregados.
- 30 exports limitados para console, contexto de thread, heap, TLS, ambiente, tempo e ExitProcess; aliases API-set exatos.

Os seis executaveis originais hello.exe, runtime.exe, dlltest.exe, dllexit.exe, tlstest.exe e ntmemory.exe rodam em ring 0, em uma pilha de aplicacao de 64 KiB, sem isolamento de memoria. ExitProcess notifica DLLs com DLL_PROCESS_DETACH na ordem inversa e restaura a pilha e registradores do shell; GS tambem e restaurado. Apenas esses seis executaveis estao autorizados pelo carregador a executar. Isso nao equivale a suporte geral a programas Windows.

## Comandos

```text
help
cls
ver
dir
type welcome.txt
run hello.wxs
run hello.exe
run runtime.exe
run dlltest.exe
run dllexit.exe
run tlstest.exe
run ntmemory.exe
nt-stub-test
nt-partial-test
nt-info-test
nt-heap-seed-test
nt-nls-test
process-context-test
nt-lock-test
nt-heap-bootstrap-test
nt-native-context-test
nt-fls-test
kb-codepage-test
nt-image-test
nt-file-test
nt-console-test
tls-test
imports cmd.exe
pe sample.pe
mem
uptime
alloc-test
page-test
vm-test
vm-query-test
pe-test
```

`fault-test` provoca UD2 e para a VM deliberadamente. `sample.pe` e uma fixture estrutural; nao e um aplicativo demonstrado. Para adicionar arquivos, coloque-os em ramfs e reconstrua: nomes ASCII simples, sem subpastas, ate 32 arquivos e 16 MiB no total. Nao ha escrita persistente nem driver de disco.

## CMD real

O build IncludeWindowsDependencies incorpora copias locais verificadas de cmd.exe e sete DLLs: combase, kernel32, kernelbase, msvcrt, ntdll, rpcrt4 e ucrtbase. O comando imports cmd.exe mapeia oito modulos em 12.976.128 bytes e resolve 3.405 imports regulares, sem chamar entry points. Isto e vinculacao de enderecos, nao implementacao dos servicos Windows. O CMD original NAO executa na ISO. O build menor IncludeWindowsCmd continua usando so o runtime original parcial (20/271 imports do CMD).

Faltam runtime C, ambiente completo de processo/thread, memoria virtual, sincronizacao, console completo, carregamento geral de DLLs, arquivos, Registro e criacao de processos. Os contratos parciais ja implementados nao substituem essas dependencias. Consulte [evidencias REA](../windows-analysis/cmd/README.md). Nao foram copiados pseudocodigos do Windows para a implementacao.

## Construir

Dependencias locais: Zig 0.14.1 (toolchain local), Python funcional com pycdlib e QEMU. Use setup.ps1 quando necessario. O comando `build.ps1` grava artefatos em `build/`; para verificar a organizacao sem alterar as ISOs existentes, utilize `./scripts/build/verify-layout.ps1`.

```powershell
./build.ps1 -Python 'caminho/para/python.exe'
# Inclui a copia local do CMD apenas para inspecao:
./build.ps1 -IncludeWindowsCmd -Python 'caminho/para/python.exe'
# Inclui o conjunto local de DLLs para preflight (build atual):
./build.ps1 -IncludeWindowsDependencies -Python 'caminho/para/python.exe'
& 'caminho/para/python.exe' ./tests/runners/test_windows.py
& 'caminho/para/python.exe' ./tests/runners/test_nt_memory_windows.py
& 'caminho/para/python.exe' ./tests/probes/probe_nt_partial_release.py
& 'caminho/para/python.exe' ./tests/probes/probe_nt_info.py
& 'caminho/para/python.exe' ./tests/runners/test_console.py
& 'caminho/para/python.exe' ./tests/runners/test_console.py --fault
& 'caminho/para/python.exe' ./tests/runners/test_console.py --page-fault
& 'caminho/para/python.exe' ./tests/runners/test_console.py --nx-fault
```

A verificacao adicional no VirtualBox usa `tests/runners/test_virtualbox.py --restart`: reinicia somente a VM Winux deste projeto, confirmada por UUID, troca sua ISO, testa pelo teclado PS/2 e deixa a janela aberta.

Desmonte a ISO antes de reconstruir se o VirtualBox mantiver o arquivo bloqueado. O teste do CMD usa a versao local analisada e requer IncludeWindowsCmd. test_windows.py deve ser executado antes de test_console.py para comparar os hashes dos seis EXEs e duas DLLs. O teste inicia e encerra sua propria VM QEMU, sem discos fisicos nem rede de convidado; serial e QMP usam loopback.

## Evidencias 0.15

- build/console-test.json e console-serial.log: 33 verificacoes aprovadas, incluindo regioes virtuais, rollback por falta de memoria, CR3 proprio, alias virtual, remapeamento/TLB, execucao PE, imports, reutilizacao da arena, teclado, timer e arquivos.
- build/page-fault-test.json e nx-fault-test.json: escrita em pagina somente leitura e execucao em pagina NX produzem excecao 14 com erros 3 e 0x11, respectivamente, no endereco virtual 0x100000000. Cada teste fatal roda numa VM QEMU separada.
- build/windows-runtime-test.json: testes dos 28 imports no console real Windows, com janela oculta; saida nao capturada, exit code zero.
- build/windows-dlltest-test.json: o mesmo dlltest.exe e suas duas DLLs passam no Windows; hashes conferidos na imagem testada.
- build/windows-hello-test.json: o mesmo hello.exe retorna zero no Windows.
- build/fault-test.json: UD2 alcanca nosso vetor 6.
- build/console.png: captura real no QEMU.
- build/virtualbox-0.15-test.json, virtualbox-0.15-serial.log e virtualbox-0.15.png: resultado da verificacao adicional no VirtualBox registrado separadamente.
- build/ramfs-manifest.json e sha256.json: origens e hashes.

## Limites

A etapa 0.15 acrescenta NtQueryInformationProcess classe 0x24 (cookie de 4 bytes) e NtQuerySystemInformation classe 0 (estrutura x64 de 64 bytes). `nt-info-test` executa os dois stubs originais por SYSCALL e compara nove registros normalizados com o Windows: tamanhos exatos, codigos, retorno de tamanho, preservacao do restante do buffer e estabilidade do cookie. CPU, RAM e limites de endereco descrevem o convidado, nao o host. A contagem de RAM deriva dos descritores UEFI de loader/servicos/conventional/ACPI; isso nao amplia o pool de alocacao disponivel.

`nt-heap-seed-test` chama o auxiliar original em RVA 0xa068, identificado nos dossies REA, apos validar seus 33 bytes. O auxiliar retorna duas vezes, inicializa seu estado RunOnce e usa duas consultas de processo. Isso nao e execucao completa de RtlCreateHeap. O cookie experimental usa RDRAND quando disponivel e um fallback de contador/tempo sem garantia criptografica; nao constitui uma protecao de seguranca equivalente a Windows. Os programas continuam confiaveis e em ring 0. [Evidencias e limites](../windows-analysis/cmd/process-system-info-runtime.md).

A etapa 0.14 acrescentou liberacao parcial de prefixo, meio e sufixo, com divisao das alocacoes e preservacao das paginas restantes. `nt-partial-test` usa os stubs originais NTDLL por SYSCALL e compara 36 registros com o Windows local; testa tambem esgotamento de metadados sem alteracao da reserva e remocao de prefixo de uma reserva de 2 MiB. O REA confirmou esse requisito no caminho de RtlCreateHeap. [Evidencias, quatro dossies e requisitos seguintes](../windows-analysis/cmd/heap-prerequisites-followup.md).

A etapa 0.13 acrescentou uma entrada SYSCALL experimental: `nt-stub-test` chama os stubs originais de quatro exports da NTDLL local, confere seus 24 bytes e numeros de servico e executa seis chamadas de memoria. A entrada troca para pilha propria, busca os argumentos adicionais na pilha original e despacha para os adaptadores. O teste tambem rejeita um numero desconhecido e confere a devolucao das paginas. Roda duas vezes no QEMU e uma no VirtualBox. O arquivo Microsoft nao e modificado e nenhum DllMain/TLS callback Microsoft e executado.

Esta transicao parte de CPL0 e retorna a CPL0, por salto ao endereco salvo em RCX. Nao ha SYSRET, ring 3, SWAPGS, SMP ou reentrancia. A pagina em 0x7ffe0000 tem apenas o seletor de SYSCALL (+0x308) implementado; nao e KUSER_SHARED_DATA completo. O caminho INT 0x2e nao e suportado. O despacho so e habilitado durante o teste; o despacho inclui quatro operacoes de memoria e as duas consultas restritas da etapa 0.15.

A etapa 0.12 acrescentou quatro bindings diretos de imports ntdll.dll: NtAllocateVirtualMemory, NtProtectVirtualMemory, NtFreeVirtualMemory e NtQueryVirtualMemory, implementados originalmente em src/nt_memory.h. O mesmo ntmemory.exe roda no Windows e na ISO e compara 11 registros de status/parametros. Recommit conserva os dados e atualiza a protecao; corrigimos a suposicao anterior de que a protecao seria conservada. Ao encerrar um executavel, as regioes desse ambiente de processo unico sao liberadas.

Esses bindings sao apenas para os testes originais confiaveis: nao interceptam a NTDLL Microsoft no grafo do CMD, usam chamada direta em vez da entrada SYSCALL, e continuam em ring 0. Suportam apenas handle -1, ZeroBits zero, protecoes READONLY/READWRITE/EXECUTE_READ/EXECUTE_READWRITE, reserva automatica e commit de intervalos na janela. Reserva em endereco solicitado, outras politicas/protecoes e consultas fora da janela retornam STATUS_NOT_SUPPORTED. Validacao de buffers ainda segue as faixas confiaveis do runtime, sem isolamento nem tratamento geral de ponteiros de usuario. O CMD nao executa.

Na 0.11, `vm_query` preenche o layout x64 de 48 bytes de MemoryBasicInformation para a janela gerenciada. Conserva a base de alocacao, arredonda o endereco consultado para pagina e agrupa paginas subsequentes de mesmo estado/protecao. `vm-query-test` compara 11 observacoes com chamadas reais NtAllocateVirtualMemory/NtProtectVirtualMemory/NtFreeVirtualMemory/NtQueryVirtualMemory no processo Python local, registradas em build/windows-nt-memory-test.json. Os testes QEMU e VirtualBox exigem igualdade das linhas normalizadas. Campos indefinidos de paginas livres e Protect de paginas reservadas nao sao usados como contrato. O tamanho livre total do Windows nao e comparado com nossa janela limitada. Isso verifica o formato e transicoes especificas, nao a transicao de syscall ou o gerenciador Windows completo.

A etapa 0.10 acrescentou `src/vm_regions.h`: reserva sem compromisso fisico, commit zerado, recommit sem destruir dados, protecao, decommit e release. Agora sao ate 16 descritores de regioes/fragmentos, reservas de ate 2 MiB com busca de espaco alinhado a 64 KiB, compartilhando o pool de 256 paginas (1 MiB fisico total). O commit desfaz suas novas alocacoes se faltar memoria; decommit permite recommit zerado. A interface ainda e interna, exige intervalos alinhados a 4 KiB e nao implementa escolha de endereco, PAGE_GUARD ou syscalls. `vm-test` verifica essas transicoes com acessos reais da CPU e confirma que o pool inteiro volta a ficar disponivel. ntmemory.exe usa esse gerenciador pelos quatro adaptadores NT limitados.

A GDT basica e propria (CS0x08, dados0x10), assim como as tabelas de paginas. Os primeiros 4 GiB continuam com mapeamento identidade supervisor, gravavel e executavel; a janela virtual de 256 MiB acima disso permite paginas de 4 KiB. Seus aliases fisicos continuam acessiveis, portanto as protecoes demonstradas nao isolam aplicativos. O pool de tabelas tem 64 paginas, alocadas conforme necessario, sem reclamacao das tabelas vazias; a janela nao tem garantia de mapeamento completo. O boot exige os buffers essenciais e framebuffer abaixo de 4 GiB e CPU com NX e paginacao de quatro niveis.

Nao ha modo usuario, scheduler, conjunto geral de syscalls NT, isolamento, WDM, USB ou SMP. Excecoes interrompem o sistema. O pool reservado nao e um gerenciador geral de RAM. A arena PE de 16 MiB aceita apenas nossos executaveis de teste confiaveis e os stubs explicitamente verificados; nao e uma fronteira de seguranca. `page-fault-test` e `nx-fault-test` interrompem deliberadamente a VM.

HLT reduz polling e a renderizacao normal atualiza apenas celulas alteradas. Nao foi medido ganho percentual de desempenho. Os proximos fundamentos sao alocacao fisica geral, interface NT para memoria virtual, ambiente de processo/thread e execucao isolada, antes de ampliar a compatibilidade do CMD.

Referencias: [PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), [UEFI](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html), [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

[Contratos e limites da versao 0.15](COMPATIBILITY.md). A meta de compatibilidade Windows ampla continua aberta; nao foi lida toda a base do Windows e nao ha benchmark que comprove ser mais leve.

As DLLs sao criadas de fontes originais em apps/. finalize_dlls.py monta tabelas de exports explicitas nos dois artefatos gerados (incluindo ordinal NONAME e forwarders), pois o caminho build-lib usado nao preserva todos esses metadados. Nenhuma DLL Microsoft e modificada.

A etapa 0.8 testa lpReserved nao nulo no encerramento normal e por ExitProcess. A mesma DLL de teste imprime DLL_EXIT_NOTIFY_OK no Windows e na ISO. Isso nao comprova suporte a threads adicionais, CRT Microsoft ou DLLs arbitrarias.

Na etapa 0.8, a arena e RAMFS foram ampliadas para 16 MiB; o numero de modulos continua limitado a16. O build local verifica hashes dos arquivos inventariados e do apisetschema.dll antes de empacotar; atualizacoes Windows exigem regenerar o inventario. O mapa API-set suporta aliases por modulo pai/default e revisao de contrato no formato local v6. Dependencias futuras por LoadLibrary e delay imports nao estao carregadas. O build contendo DLLs Microsoft e um artefato local de pesquisa, distinto do build original sem esses arquivos.

TLS estatico inicial: vetor no contexto x64, copia por modulo, indice, alinhamento e callbacks na thread unica. Testes Windows e ISO usam as mesmas instrucoes GS:[0x58]; o verificador rejeita o teste se essas instrucoes ou o template estiverem ausentes. Nao ha novas threads/fibers nem carregamento TLS dinamico. imports cmd.exe prepara os tres templates declarados nas DLLs reais, sem executar callbacks Windows.
