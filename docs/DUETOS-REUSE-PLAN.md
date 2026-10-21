# Integração Winux + DuetOS + engenharia reversa Windows

Data: 21/10/2026. Escopo: primeiro lote integrado e comprovado no Winux.

## Objetivo e fontes

**Kernel principal:** Winux, com boot UEFI, paginação própria e carregador PE
limitado. **Referência arquitetural:** DuetOS local em
\`../../external-references/DuetOS\` (o caminho é relativo ao workspace, não
ao repositório), commit avaliado
19bf519d8d699f92000475c97831d7c357b13abc.

**Fonte de comportamento Windows:** investigações REA/Ghidra, fixtures e
inventários em windows-analysis/, com hashes NTDLL/KernelBase/MSVCRT fixados.
Esses contratos têm precedência sobre aproximações ou retornos fictícios.

O código-fonte do DuetOS está sob MIT (Copyright 2026 DuetOS contributors);
os arquivos originais do projeto de referência permanecem fora do Winux.
**Nenhum arquivo nem trecho substancial de implementação do DuetOS foi copiado
para o kernel nesta rodada.** A licença/commit são registrados para futura
proveniência se a necessidade de portar um algoritmo específico for demonstrada.

## Decisão de integração

Não criar uma segunda camada de serviço enquanto já existe um dispatcher em
src/nt/nt_syscall.h. Sua numeração pertence à versão analisada da NTDLL, e
atualmente 16 rotas são reconhecidas com funcionalidade parcial. Esta
numeração **não é uma ABI nativa estável do Winux**.

Foi aplicado ao dispatcher existente o **padrão declarativo** visto no DuetOS:
abi/nt_services_19041.json gera abi/generated/nt_services_19041.h. Os nomes
gerados são usados tanto no switch quanto na função de validação do intervalo
de argumentos na pilha do chamador. O gerador executa verificações de
integridade e build.ps1 recusa divergências.

A funcionalidade de identificação de chamadas NT ausentes foi implementada
**no mesmo dispatcher**: tabela fixa de 16 identificadores/contadores;
comandos nt-missing/nt-missing-reset no shell, e cruzamento posterior com
os exports originais da NTDLL. Não há um segundo objeto de handles, heap,
scheduler, loader PE ou camada duplicada de kernel.

A análise lexical confirmou equivalência de 3.022 tokens do shell de produção
com cada candidato P1–P4, removendo apenas as inclusões de módulos e as novas
linhas de diagnóstico. Portanto, os quatro shell.h experimentais viraram
wrappers de três linhas que usam o único src/shell/shell.h e selecionam seu
startup_test.h. Os boot.c e os testes diferentes das variantes permanecem
independentes.

## Evidências de execução

- Host: tests/runners/test_nt_abi_contract.py exige os 16 IDs do inventário
  Windows da mesma NTDLL, compila o header e verifica números/argumentos.
- Guest: tests/runners/test_console_wrapper_fls_candidate.py aceita a ISO
  experimental e --check-nt-missing; regressão QEMU aprovou **38 verificações**.
- Operação desconhecida deliberada (0xffffffff): retorna NTSTATUS 0xc000001c;
  no log, aparece 4294967295 com 2 chamadas e nenhum overflow.
- Cross-reference: scripts/compatibility/analyze_nt_missing.py distingue
  serviço NT reconhecido, já despachado e valor desconhecido de teste.
- ISO de trabalho: build/abi-preview/winux-0.38-wrapper-fls-candidate.iso;
  SHA-256 12821d594c1ab471687ce7e69851ac50ecc1082dfb3a25f37b1b4beac8bbedfc.
- ISOs históricas 0.38 e P4 não foram sobrescritas:
  700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0
  e 4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77.

## Planejamento condicionado a provas

| Etapa | Critério de saída antes de evoluir |
|---|---|
| P0 — Entrada e saída segura CPL3 | TSS/RSP0, GDT e segmentos de usuário, pilha protegida, gate, faults de usuário recuperáveis, validação de buffers, retorno seguro, provas de isolamento |
| P1 — Processos e objetos | Espaços de endereço e ciclo de vida próprios, handles por processo, teardown, sincronização, sem executar binários desconhecidos no CPL0 |
| P1 — ABI nativa Winux | Introduzir somente syscalls consumidas por fixture real em CPL3; contrato versionado; sem reutilizar números NT ou DuetOS |
| P1 — DLLs Win32 próprias | Implementar kernel32/ntdll como adaptadores por domínio, export por export, com diferencial Windows e testes QEMU |
| P2 — CMD real | cmd.exe /d /c echo teste, stdout + exit code + limpeza verificados; 81/271 nomes do DuetOS são apenas coincidência nominal |
| P3 — CLR/GUI/drivers | Trilhas separadas após serviço de processo, arquivos, exceções e sincronização maduros |

O DuetOS **não garante execução nativa** dos programas-alvo nem entrega CLR
operacional por meio dos nomes de exports. Para o Winux, uma função só será
marcada compatível quando o mesmo contrato tiver sido exercitado na ISO e
comparado com o Windows de referência.

## Próximo comando de validação

No diretório raiz deste repositório:

\`\`\`powershell
.\scripts\build\verify-layout.ps1 -RunHostTests -RunQemu
\`\`\`

O script compila as variantes, verifica o contrato, gera uma ISO *preview*
separada, testa QEMU e produz build/nt-missing-cross-reference.json.

**Limites:** um núcleo, CPL0 para os fixtures, dispatcher NT não reentrante,
sem ring 3, processo isolado, CLR ou CMD original. Não houve benchmark
equivalente entre Winux e Windows. A P4 com ABI é experimental, ainda não
homologada em VirtualBox, Proxmox ou hardware.
