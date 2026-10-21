# ABI efetivamente usada pelo Winux

O arquivo **nt_services_19041.json** define os identificadores NT já implementados
(parcialmente) no dispatcher em src/nt/nt_syscall.h. Não é uma segunda interface
de syscall e não define a futura ABI nativa do Winux.

- Perfil: NTDLL x86-64 de Windows 10, identificado pelo SHA-256
  36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d.
- O gerador scripts/build/generate_nt_abi.py produz
  abi/generated/nt_services_19041.h; esse header é usado diretamente pelo
  dispatcher existente. O build.ps1 recusa compilar se houver divergência.
- Os 16 identificadores têm contrato limitado de tamanho de argumentos na
  pilha, para o caminho experimental CPL0→CPL0.
- Eles não podem ser usados como números estáveis do Winux nem transferidos a
  uma NTDLL de outra versão sem nova análise.
- O campo caller_stack_span_bytes registra o intervalo já requerido pelo
  adaptador atual; não representa validação completa de ponteiros de usuário.

## Testes

No diretório raiz Winux:

    & "C:\Program Files\Python310\python.exe" scripts/build/generate_nt_abi.py --check
    & "C:\Program Files\Python310\python.exe" tests/runners/test_nt_abi_contract.py

A segunda verificação confronta **todos os 16 IDs** com os exports originais
presentes no inventário de NTDLL em windows-analysis/cmd/nt-stubs.json, exige a
mesma identidade binária e compila o header em um teste host Windows.

## Diagnóstico de serviços ausentes

O dispatcher original mantém resposta STATUS_NOT_IMPLEMENTED (0xC000001C) para
números não reconhecidos. Foi acrescentado apenas um registro delimitado de
16 IDs distintos, com contagem saturada e contabilização de excesso. Não captura
argumentos, endereços de chamadores ou conteúdo de buffers. O shell utiliza:

    nt-missing
    nt-missing-reset

O script scripts/compatibility/analyze_nt_missing.py cruza o último bloco
completo de telemetria com os nomes presentes no inventário NTDLL da versão
identificada. Números sem correspondência são sinalizados como desconhecidos,
não como APIs implementadas.

## ABI nativa futura

Os serviços nativos para processos em CPL3 **não foram introduzidos** neste
marco: não existe TSS/RSP0 completo, cópia segura de parâmetros de usuário,
espaços de endereço por processo, escalonador preemptivo ou saída segura de
processo. O projeto não ganhou compatibilidade Windows generalizada.

Uma ABI Winux-nativa versionada deverá ser criada **somente no momento de
integrar seu primeiro consumidor real em CPL3**, aproveitando os serviços
existentes de memória, console e objetos, sem duplicar o dispatcher NT.
