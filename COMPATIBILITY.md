> Etapa atual 0.10: gerenciador interno de reserva/commit/protecao/decommit/release com rollback por esgotamento, sobre as tabelas proprias da etapa 0.9. Limites: 16 regioes de ate 1 MiB, pool fisico compartilhado de 1 MiB, uma thread e nenhum isolamento. Nao implementa ainda NtAllocateVirtualMemory, consulta NT, alocador fisico geral ou transicao SYSCALL. A etapa 0.8 acrescentou TLS estatico limitado; as limitacoes de TLS nas secoes historicas 0.4-0.7 descrevem aquelas etapas. O CMD segue sem execucao.

> Estado atual 0.7: o build com DLLs locais resolve os 3.405 imports regulares do conjunto, mas nao executa o CMD. Numeros 251 pendentes nas secoes anteriores descrevem o build sem DLLs originais.

# Contratos Windows: estado verificavel em Winux 0.15

Etapa 0.15: duas consultas NT adicionais, comparacao de nove registros de formato/erro e execucao do auxiliar original de preparacao do gerador do heap. O estado RunOnce e inicializado e duas chamadas de processo sao observadas no despacho. Nao ha prova de RtlCreateHeap completo, CRT ou CMD. Veja [process-system-info-runtime.md](../windows-analysis/cmd/process-system-info-runtime.md). As afirmacoes anteriores de ausencia dessas duas consultas sao historicas.

Etapa 0.14: liberacao parcial via NTDLL original, com 36 registros comparados ao Windows; enderecos e metadados desacoplados, ate 16 descritores incluindo fragmentos e reservas de ate 2 MiB. Pool fisico permanece 1 MiB. O caminho de RtlCreateHeap que libera um prefixo foi confirmado em quatro novos dossies REA; o heap completo e o CMD ainda nao executam. Veja [heap-prerequisites-followup.md](../windows-analysis/cmd/heap-prerequisites-followup.md).

Etapa 0.13: [stubs originais da NTDLL executados por SYSCALL](../windows-analysis/cmd/syscall-runtime.md), com pilha propria e quatro servicos de memoria. Continua CPL0 para CPL0, sem processo Windows completo ou inicializacao de DLLs Microsoft. Os paragrafos abaixo sobre ausencia de transicao descrevem etapas anteriores; suporte geral NT permanece ausente.

Etapa 0.12: quatro adaptadores de memoria com assinatura NT, ligados diretamente aos imports do novo teste original ntmemory.exe. Onze registros comparados com o mesmo PE no Windows; detalhes e restricoes em [nt-memory-adapters.md](../windows-analysis/cmd/nt-memory-adapters.md). Recommit agora atualiza protecao e conserva dados. Nao ha transicao SYSCALL ou execucao da NTDLL original na ISO. As descricoes anteriores de ausencia de qualquer interface NT sao historicas; suporte geral NT continua ausente.

Etapa 0.11: consulta interna MemoryBasicInformation (48 bytes em x64) e comparacao de 11 observacoes com a API NT no Windows local. Testa consultas no interior de regioes, divisao por protecao, decommit e release; o estado livre e comparado sem seu tamanho, especifico de cada endereco virtual. A API de consulta funciona apenas na janela gerenciada, sem syscall, outros processos, imagens mapeadas ou classe 3. O paragrafo 0.10 acima descreve a etapa anterior; consulta basica passou a existir na 0.11.

A meta de executar programas Windows reais permanece aberta. Esta versao e uma etapa original de implementacao; nao contem uma base Windows em execucao. Os arquivos Microsoft opcionais sao copias locais para pesquisa, sem alteracao da instalacao Windows.

## Evidencias e escopo

- REA: CMD 10.0.19041.4522, SHA-256 badf4752413cb0cbdc03fb95820ca167f0cdc63b597ccdb5ef43111180e088b0. Dez dossies anteriores mais startup-cookie.dossier.json, evidencia ev_47384a9e626f51bfb6101e4937ebc95d624607f934253182f248379d99fad12e.
- A primeira chamada da entrada, VA 0x140019584, prepara o cookie a partir de GetSystemTimeAsFileTime, IDs, GetTickCount e QueryPerformanceCounter. Isto e observacao estatica, nao execucao do CMD na ISO.
- O startup seguinte le GS:[0x30] e o StackBase do NT_TIB. Por isso criamos um contexto de thread proprio, sem copiar codigo decompilado.
- runtime.exe importa 28 funcoes. O mesmo SHA-256 precisa passar no console Windows e estar incorporado no BOOTX64.EFI da ISO testada. O teste verifica essa identidade.
- CMD: 271 imports regulares, 20 resolvidas, 251 ausentes. Ha ainda 15 delay-imports e dependencias transitivas nao resolvidas. Contagem de imports nao mede percentual de compatibilidade.

## Implementado com limites explicitos

| Area | Contrato implementado | Limites atuais |
|---|---|---|
| Contexto | GDT propria (CS0x08, dados0x10), recarga GS e verificacao de Self antes da entrada; NT_TIB Self, StackBase/Limit; IDs, ponteiro PEB, last-error; GS instalado/restaurado | Uma thread, IDs 1/1; TEB parcial de x64 e PEB apenas com ImageBase. Sem PEB loader lists, parametros completos ou WOW64. |
| Execucao | Pilha de aplicacao de 64 KiB; retorno da entrada e ExitProcess | Ring 0, sem protecao, um executavel por vez. ExitProcess restaura RSP, registradores nao volateis inteiros, estado x87/SSE por FXSAVE64/FXRSTOR64 e depois GS; com DLL detach; sem atexit CRT. Falhas ainda param o kernel. |
| Heap | GetProcessHeap, HeapAlloc/Free/ReAlloc/Size | 256 KiB fixos; alinhamento de 16; zero opcional; split/coalesce; realloc conserva bytes; falha conserva bloco antigo. Um heap, sem HeapCreate, excecoes de heap ou crescimento. |
| TLS | TlsAlloc/Free/SetValue/GetValue | 64 slots da unica thread; zero inicial; GetValue limpa last-error no sucesso. Sem TLS directory/callbacks ou FLS. |
| Ambiente | GetCommandLineW, Get/SetEnvironmentVariableW | Linha de comando apenas nome do arquivo, ate 255 unidades; 16 variaveis, nomes de ate 63 e valores de ate 255 unidades UTF-16; case folding apenas ASCII. COMSPEC/SystemRoot/PATH sao strings, nao comprovam diretorios existentes. |
| Console | GetStdHandle, WriteFile, WriteConsoleW, Get/SetConsoleMode, Get/SetConsoleOutputCP | WriteFile sincrono stdout/stderr, ate 64 KiB, somente CP_UTF8 (65001), sem redirecionamento. Bytes UTF-8 sao decodificados antes de desenhar; nao sao convertidos implicitamente como Latin-1. WriteConsoleW ate 32768 unidades UTF-16 e contagem exata; surrogate pairs decodificados; modos aceitos apenas defaults implementados. Sem ReadConsole, screen-buffer API geral ou codepage conversion. |
| Fonte | ASCII e acentos latinos compostos | Outros escalares preservados no serial UTF-8, exibidos como ? no framebuffer. Sem shaping, caracteres combinantes ou largura dupla. |
| Tempo | GetTickCount/64, QueryPerformanceCounter/Frequency | Contador PIT de 100 Hz (10 ms), nao um contador de alta resolucao. Sem wall clock/GetSystemTimeAsFileTime. |
| Resolucao | 30 exports de runtime; grafo estatico de DLLs RAMFS; imports por nome/ordinal; exports de funcao/dados; forwarders por nome/ordinal; GetModuleHandleW e GetProcAddress | Ate 16 modulos na arena de 16 MiB; sem LoadLibrary/FreeLibrary, TLS callbacks, delay-load, CRT ou busca de DLLs no disco. Ciclos nao validados. |

Os acessos GS no teste comparam Self, limites da pilha, IDs, ponteiro PEB e LastError com o comportamento observado no Windows x64 local. A estrutura e parcial e dependente dessa arquitetura; preencher esses campos nao comprova suporte a todo acesso interno de qualquer versao Windows.

## Testes

`test_windows.py`: hello.exe em stdout capturado; runtime.exe em console Windows real com janela oculta, sem redirecionamento. O segundo registro confirma exit code, nao captura saida. Codigos de falha distintos estao em apps/runtime_win32.c.

`test_console.py`: os mesmos executaveis na ISO em QEMU, com serial UTF-8 e screenshot. Verifica contexto/pilha, erros, heap/alinhamento/zero/realloc/falha/reuso, TLS, ambiente (incluindo reset entre invocacoes), UTF-16, contador, ExitProcess, restauracao GS e MXCSR/x87 (teste muda o arredondamento antes de ExitProcess) e retorno ao shell. O teste de contexto deve passar novamente depois de inspecionar o CMD. `--fault` cobre o tratador fatal UD2, sem prometer recuperacao.

Os artefatos atuais sao build/windows-runtime-test.json, console-test.json, fault-test.json, ramfs-manifest.json e sha256.json. Resultados do VirtualBox sao separados; nao se presume resultado desse hipervisor a partir do QEMU.

## Requisitos ainda abertos da meta completa

- Ler e seguir as dependencias e callbacks reais do CMD, CRT e DLLs, com evidencias por versao. Toda a base do Windows ainda nao foi analisada.
- Ampliar a paginacao propria para isolamento; TSS/IST, alocador fisico, modo usuario, processos/threads isolados, syscalls e mecanismos de excecao/unwind.
- Heap/memoria virtual amplos, sincronizacao, CRT (incluindo imports de dados), PEB/process parameters, carregamento de DLLs e recursos/MUI.
- Arquivos/dispositivos/handles persistentes, I/O e modelo de drivers, Registro, console completo, filhos e pipes.
- Executar o CMD original sem alterar bytes, primeiro /d /c echo teste, depois comandos interativos e programas/driver selecionados.
- Medir consumo e desempenho em cargas comparaveis antes de afirmar que o sistema e mais leve. HLT e framebuffer simples nao provam essa meta.

Referencias publicas de contrato: [TEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb), [HeapAlloc](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc), [TlsGetValue](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue), [GetEnvironmentVariableW](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablew), [WriteConsole](https://learn.microsoft.com/en-us/windows/console/writeconsole), [PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).

## Regressao observada no VirtualBox NEM/WHV

Durante a verificacao a frio, snapshots de CW x87 antes/depois mostraram 0x0000 e 0x037f, apesar da inicializacao por FNINIT; MXCSR era 0x1f80 nos dois. O controle de saida detectou a diferenca e devolveu falha95. Em outra tentativa o teste de Self retornou10. Esses resultados nao foram tratados como sucesso.

O backend NEM/Hyper-V foi identificado no VBox.log local. A causa exata do estado inicial nao foi comprovada. A versao atual inicializa CR0/CR4 para FPU/SSE, executa FNINIT seguido de FLD1/FSTP (pilha x87 vazia, uso explicito), carrega MXCSR0x1f80, salva/restaura contexto x87/SSE completo e serializa a entrada apos escrever codigo/GS. Uma primeira verificacao a frio passou com esse conjunto, mas outra expos falha de GS (registro build/virtualbox-0.4-gs-regression.json). Acrescentamos GDT propria, recarga do seletor GS e verificacao efetiva de Self antes de chamar o programa. Resultados posteriores precisam usar o hash da imagem atual; o primeiro sucesso antigo nao comprovava estabilidade. Os registros por hash ficam em build/virtualbox-0.4-first-cold-test.json e virtualbox-0.4-test.json. Isto registra um comportamento observado nesse ambiente, nao uma afirmacao de bug confirmado no VirtualBox.

Comparacoes verificam os bits de controle definidos: MXCSR0xffc0 e CW0x1f3f, excluindo status volatil e bits reservados. A demo muda o arredondamento antes de ExitProcess e a restauracao precisa passar antes de marcar PE_EXEC_OK.

A [referencia Intel](https://www.intel.com/content/dam/develop/external/us/en/documents/319433-024-697869.pdf) define o estado inicial x87. O [backend NEM do VirtualBox](https://raw.githubusercontent.com/mirror/vbox/master/src/VBox/VMM/VMMAll/NEMAllNativeTemplate-win.cpp.h) transfere o controle FP via WHV; esse codigo nao prova sozinho a origem da diferenca. A responsabilidade por sincronizar codigo escrito e executado tambem esta descrita no contrato de [FlushInstructionCache](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache).

Verificacao historica da etapa 0.4: duas partidas a frio no VirtualBox com a imagem SHA-256 48ecb943f850e56e6c39bd9340073ab7305de1cb9b95346590b43da07d9c3ee3, mais console e UD2 no QEMU, todos aprovados. hello.exe e runtime.exe tambem passaram no Windows com hashes iguais aos incorporados. Isto confirma os contratos de teste descritos, nao a execucao do CMD ou a meta completa.

## Etapa 0.5: DLLs originais e evidencia CRT

Os mesmos bytes de hello.exe, runtime.exe, dlltest.exe, winuxrt.dll e winuxbridge.dll passaram no Windows e estao incorporados no kernel testado no QEMU. O VirtualBox passou nos tres EXEs, incluindo ordem de attach/detach das DLLs. ISO SHA-256: e32b3f18f5c043fa87d1b3bb2d16566f99669ec606f2bca87a924bc1134a26d4. Evidencias: build/windows-dlltest-test.json, console-test.json (20 verificacoes), fault-test.json e virtualbox-0.5-test.json.

DllMain attach roda apos resolver imports e antes da entrada principal; o retorno normal chama detach na ordem inversa. Se um attach falha, as DLLs previamente anexadas recebem detach. Esse caminho de falha tem revisao de codigo, mas ainda nao tem fixture de execucao. Na etapa historica 0.5, ExitProcess ainda saltava para a restauracao do contexto sem DLL detach; nao ha notificacoes de thread nem atexit CRT. Nao declarar semantica completa de encerramento Windows. GetProcAddress nao carrega novas DLLs durante execucao; forwarders so podem alcançar modulos ja carregados ou exports internos. Nao ha fronteira de seguranca para binarios arbitrarios.

O teste cobre import estatico por ordinal NONAME, funcao, dado compartilhado, consulta dinamica, ordinal por GetProcAddress e forwarders de funcao/dado/ordinal. As DLLs sao originais; finalize_dlls.py gera seus metadados PE. Nenhum byte decompilado da Microsoft foi incorporado.

REA de _initterm em msvcrt.dll: VA de analise 0x11013a510, evidencia ev_82770af4c301bc56ab3bced6237f75faefdb59bc0fc3f444e6240b971db30d08, em ../windows-analysis/cmd/crt-initterm.dossier.json. Observado percurso de ponteiros de callbacks, ignorando entradas nulas e realizando chamada indireta; o decompilador registra substituicao da chamada guardada. Nao comprova callbacks executados nem ambiente CRT funcional. O inventario independente encontra 85 exports de codigo e dois de dados usados pelo CMD, e 154 imports diretos da CRT. Consulte ../windows-analysis/cmd/crt-dependencies.md.

A contagem CMD permanece 20/271 resolvidas e 251 pendentes: GetModuleHandleW/GetProcAddress novos no runtime nao reduzem automaticamente os contratos exatos importados por esse CMD. A entrada original continua bloqueada. Toda a base Windows continua por analisar e a meta completa nao esta concluida.

Referencias adicionais: [GetProcAddress](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress) e [DLL entry point](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-entry-point-function).

## Etapa 0.6: encerramento de DLLs

A ISO atual e build/winux-0.6-x64.iso, SHA-256 c3f98eb2c9a5dbc7b4cd74cf5ee99d341e16e567c27e4fbf58ee3a845f46a038. Quatro EXEs e duas DLLs originais passaram no Windows, com identidade dos bytes incorporados conferida no QEMU. Console: 21 verificacoes; UD2 e VirtualBox tambem passaram (build/virtualbox-0.6-test.json).

ExitProcess preserva seu codigo de saida, notifica DLL_PROCESS_DETACH em ordem inversa com parametro reservado nao nulo, e so entao restaura pilha/estado FP e retorna ao shell. A entrada principal retornando normalmente segue o mesmo encerramento. DLL_PROCESS_ATTACH estatico tambem recebe reservado nao nulo. Rollback de attach falho usa reservado nulo e inclui a DLL que devolveu falso, mas esse caminho ainda nao tem teste de execucao. Notificacoes de thread, locks do loader, descarregamento dinamico e encerramento concorrente continuam ausentes. O teste WriteFile dentro de detach apenas observa o contrato em nosso programa simples; nao valida cleanup arbitrario.

O ultimo conjunto de testes sobrescreve console-test.json e windows-*-test.json; os hashes historicos acima identificam etapas anteriores, nao os registros atuais. Referencias: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess e https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain.

## Etapa 0.7: mapeamento das DLLs originais

ISO local build/winux-0.7-x64.iso: SHA-256 4f6c80e5b43f68f8693a686ffe1740aac73a4524bd96883d924bd8eebfd54cb7. RAMFS com 18 arquivos, 12.605.216 bytes. Arena de imagens de16MiB reservada via UEFI antes de ExitBootServices. Oito modulos do Windows usam12.976.128 bytes alinhados. Quatro testes EXE e duas DLLs proprias continuam com identidade verificada e execucao Windows/QEMU; testes VirtualBox em registro separado por hash.

prepare_windows.py verifica hashes das fontes e schema contra dependency-startup.json. prepare_assets.py incorpora copias de bytes; os arquivos System32 permanecem inalterados. A resolucao do grafo original desliga substituicoes por exports do runtime proprio: a IAT aponta aos exports mapeados das DLLs originais. Aliases API-set usam modulo importador/forwarder como pai. O kernel usa busca linear no mapa v6 gerado, com fallback de revisao unica; nao ha manifests/SxS ou busca arbitraria em disco.

imports cmd.exe: oito modulos mapeados, relocacoes aplicadas e 3.405 imports regulares sem pendencias. Nenhum entry point executado. run cmd.exe permanece bloqueado. Zero imports pendentes NAO significa que chamadas de sistema NT, TLS, PEB loader lists/process parameters, exception unwind, sincronizacao ou dispositivos estao implementados. As entradas originais nao foram chamadas e a inicializacao CRT real nao passou. Os diretorios TLS presentes em CRT/KernelBase/Combase continuam sem suporte.

A suite console confirma hashes e bytes incorporados dos oito arquivos, ausencia de entrada/attach, bloqueio de execucao e reutilizacao da arena por programas originais apos preflight. Resultados atuais: build/console-test.json (22 verificacoes), fault-test.json, windows-*-test.json e virtualbox-0.7-test.json. Estes registros nao substituem uma futura prova de execucao Windows.

## Etapa 0.8: TLS estatico da thread unica

ISO SHA-256: 8bd4d1aadba385687f51b22206b7b9e152022df97c873ad47a6dab31b5860d4c. Cinco EXEs originais e duas DLLs testadas; RAMFS com19 arquivos,12.611.360 bytes. A suite QEMU tem24 verificacoes, incluindo integridade dos mesmos bytes Windows, testes de limites TLS e preflight das DLLs reais. VirtualBox registrado por hash em build/virtualbox-0.8-test.json.

src/pe_tls.h valida o diretorio PE32+, VAs relocadas, template, indice, callbacks dentro de secoes executaveis e alinhamento. Copia templates para pool de64KiB, escreve indices e publica vetor de16 slots em TEB+0x58. Esse vetor e separado dos slots TlsAlloc/TlsGetValue. Os tres templates originais Windows sao preparados (40bytes com alinhamentos), mas suas entradas e callbacks nao sao executados. Nao ha modo usuario, criacao de threads, FLS, carregamento dinamico, loader lock ou tratamento de excecoes de callback.

Nos testes originais, callbacks attach precedem DllMain/entrada e callbacks detach precedem DllMain detach. dllexit.exe chama ExitProcess explicitamente e confirma no Windows os dados TLS e a ordem antes de DLL_EXIT_NOTIFY_OK. O retorno direto de mainCRTStartup no Windows local nao produziu o mesmo marcador TLS de detach; nao declaramos equivalencia entre esse retorno bruto e ExitProcess. O kernel ainda trata retorno bruto como encerramento do processo, limitacao registrada.

Template das fixtures:12bytes. Os binarios usam __thread e secoes .tls$AAA/.tls$BBB/.tls$ZZZ com -fno-emulated-tls; warnings de atributos desconhecidos viram erros. test_windows.py confirma o template e instrucoes MOV de GS:[0x58] em secoes executaveis (6 no EXE,5 na DLL), alem da execucao. A primeira tentativa com __declspec(thread)/allocate foi ignorada pelo target GNU e nao comprovava TLS; build/virtualbox-0.8-inadequate-tls-test.json conserva esse resultado invalidado para esta finalidade. Todos os resultados atuais usam a imagem corrigida, com outro hash.

O comando tls-test modifica apenas o template de teste ja mapeado para rejeitar fim anterior ao inicio, indice fora da imagem, zero fill excessivo, vetor de callbacks truncado e alinhamento reservado. Restaura a imagem e o teste seguinte precisa passar. SizeOfZeroFill e tratado pelo pool zerado; o teste positivo atual usa variavel zero no template, sem uma fixture separada de zero-fill positivo. Callbacks cruzando modulos e diretorios de versoes/arquiteturas diferentes nao foram validados.

Contrato consultado: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format (TLS directory e callbacks). Dados especificos de TEB x64 permanecem limitados ao ambiente local testado, sem promessa universal. A meta Windows/NT ampla e desempenho relativo seguem incompletos.
