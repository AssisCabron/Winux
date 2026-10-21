# Winux — Arquitetura e organização do repositório

Estado: kernel original experimental 0.38, CPU x86-64, boot UEFI. O projeto não implementa compatibilidade geral com Windows, não oferece isolamento CPL3 e ainda não executa o CMD ou a CLR. O código executável é compilado com um único translation unit originado em src/kernel/boot.c; os headers atuais contêm implementações, não apenas declarações. **A movimentação dos arquivos não transforma automaticamente essa arquitetura em um kernel multiobjeto.**

## Diretórios e responsabilidade

| Diretório | Responsabilidade |
| --- | --- |
| src/kernel/ | Entrada UEFI, contexto de boot, sequência de inicialização |
| src/arch/x86_64/ | Hardware x86-64, GDT/IDT, interrupções e paginação |
| src/mm/ | Regiões de memória, alocação e adaptadores NT de memória |
| src/loader/ | Carregador PE32+, DLLs, exports/imports, relocations e TLS |
| src/process/ | Contexto PEB/TEB, parâmetros de processo e ciclo de vida nativo |
| src/nt/ | Dispatcher SYSCALL, objetos, handles, I/O e consultas NT limitadas |
| src/runtime/ | Abstrações Win32 e estado KernelBase para testes |
| src/console/ | Terminal, framebuffer, composição de linhas |
| src/shell/ | Comandos e orquestração dos testes de diagnóstico |
| src/tests/ | Fixtures compiladas no mesmo binário do kernel (subdivididas por área) |
| apps/ | Fixtures Win32 e DLLs originais do projeto usadas nos testes |
| tests/host/ | Programas C de teste executados no Windows host |
| tests/fixtures/ | Headers e dados compartilhados com os testes |
| tests/checks/ | Comparadores de registros guest/host |
| tests/probes/ | Experimentos executados no Windows para obter contratos verificáveis |
| tests/runners/ | Suítes automatizadas Windows, QEMU e VirtualBox |
| experiments/candidates/p1-p4/ | Fontes congeladas de cada variante experimental 0.38 |
| experiments/packaging/ | Geradores de ISO das variantes experimentais |
| experiments/historical_generators/ | Geradores antigos para a estrutura plana; **arquivados, não executar** |
| scripts/build/ | Assets, finalização PE, empacotamento ISO e verificação da organização |
| scripts/maintenance/ | Scripts pontuais de migração; não destinados ao ciclo normal de build |
| build/ | Artefatos gerados, snapshots de testes, logs e ISOs |
| _snapshots/ | Backups de código e estrutura anteriores à migração |

## Pontos de entrada e ordem do build

- Compilação estável: build.ps1 na raiz (aponta para src/kernel/boot.c e scripts/build/*.py).
- Boot UEFI: src/kernel/boot.c inclui console, hardware, paginação e shell.
- O shell inclui os outros subsistemas e testes em ordem dependente; **não alterar a ordem sem validação de símbolos e estado**.
- Candidatas P1–P4 ficam em experiments/candidates/pX/boot.c, com shell.h e startup_test.h locais. São variantes congeladas, não um novo branch do kernel.
- O script scripts/build/verify-layout.ps1 compila o kernel e as quatro variantes em build/layout-verification/ e compara o SHA-256 EFI aos executáveis prévios; não substitui as ISOs.

## Uso e testes

Execute no PowerShell, a partir de experimental-os:

```powershell
# Verificar identidade do kernel e variantes sem tocar nas imagens existentes:
.\scripts\build\verify-layout.ps1

# Verificar tambem cinco suites host e a regressao QEMU da candidata P4:
.\scripts\build\verify-layout.ps1 -RunHostTests -RunQemu

# Reempacotar em uma pasta isolada e conferir as estruturas UEFI:
& "C:\Program Files\Python310\python.exe" .\scripts\build\verify_packaging.py

# Build estável explícito — ATENÇÃO: grava os artefatos em build/.
.\build.ps1 -IncludeWindowsDependencies -Python "C:\Program Files\Python310\python.exe"

# Repetir apenas a regressão da ISO P4 existente:
& "C:\Program Files\Python310\python.exe" .\tests\runners\test_console_wrapper_fls_candidate.py
```

Para reproduzir uma candidata, compile a entrada experiments/candidates/pX/boot.c em seu BOOTX64 específico e use o empacotador correspondente em experiments/packaging/. Empacotadores antigos gravam no mesmo destino, portanto gere cópia externa se quiser preservar as imagens validadas.

## Regras de manutenção

1. Código de produção em src/ por subsistema, teste guest em src/tests/ e teste host em tests/. Não criar novas versões completas de shell e boot para cada API; para experimentos novos, preferir flags de compilação ou pequenas fixtures isoladas.
2. Manter os campos dependentes de versão Windows vinculados ao hash das DLLs e à evidência REA. Nunca copiar pseudocódigo Microsoft para o kernel.
3. Não liberar execução irrestrita de EXE em CPL0. Implementar ring 3, tratamento de faults e isolamento por processo antes.
4. Verificar caminho de includes, build, identidade dos hashes e regressões QEMU; validar VirtualBox antes de marcar versão estável.
5. Não excluir ISOs, toolchains ou logs históricos automaticamente. Preservar cópias e resultados comparativos.
6. Refatoração futura separada: definir interfaces .h, dividir os fontes em .c, criar linker script/política de memória e CI, com builds determinísticos e testes de regressão a cada etapa.

## Migração de 21/10/2026

- 53 arquivos do diretório src original movidos para subsistemas e experiências.
- 54 scripts Python distribuídos em scripts/, tests/ e experiments/.
- 8 fixtures C da pasta tests movidas para tests/host e tests/fixtures.
- Os includes relativos e referências de Python foram recalculados; o binário recompilado do kernel estável e das quatro candidatas manteve o SHA-256 pré-migração.
- Fontes preservadas em _snapshots/winux-pre-refactor-source.zip. Backups adicionais: winux-pre-tooling-20261021.zip e winux-host-fixtures-prelayout.zip.
- O processo não substituiu a ISO original Winux 0.38 nem as candidatas.
- Auditoria final em build/layout-audit.json: 68 fontes C/headers, 173 includes relativos resolvidos, 64 scripts Python com sintaxe válida e cinco ISOs originais com hashes preservados.
- build/layout-verification/ guarda ISOs reconstruídas isoladamente; hashes ISO novos podem diferir por metadados de mídia, embora os executáveis EFI sejam idênticos.
- A ISO P4 reempacotada (SHA-256 47bf57ea684de123917a7c9527b750835250e660045b95ac391ec56e389f6ee5) passou nos mesmos 37 grupos QEMU, com policy/FLS detectados em dois ciclos. Evidência: build/console-repacked-p4-test.json e build/console-repacked-p4-serial.log.
- A verificação executável está em scripts/build/verify-layout.ps1 e scripts/build/verify_packaging.py. Não foram executados testes adicionais no VirtualBox, Proxmox ou hardware físico.
