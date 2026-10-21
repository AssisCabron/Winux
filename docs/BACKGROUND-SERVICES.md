# Serviços residentes e processos CPL3 — Winux

Data: 21/10/2026. Recursos disponíveis apenas na ISO experimental.

## Arquitetura

O Gerenciador de Tarefas do Windows mostra processos e serviços, mas um
serviço não é necessariamente um processo separado. O Service Control
Manager coordena inicialização, falhas e dependências. O Winux precisa
de funções equivalentes nativas, sem copiar services.exe ou svchost.exe.

A primeira implementação reutiliza o código existente de criação de
espaços de endereço (wx_address_space), gate INT 80h, quadros salvos
(wx_user_resume_frame), PIT e execução cooperativa do shell. NÃO cria
outro escalonador, outro pool de memória ou outro gerenciador de handles.

## Dois serviços reais: clockd e memd

- Código comum: src/process/service_manager_preview.h
- PID 401: clockd, CPL3; executa a syscall WX_NATIVE_CLOCK_TICKS=3.
- PID 402: memd, CPL3; executa a syscall WX_NATIVE_MEMORY_FREE_PAGES=4 e
  observa o total de páginas físicas livres no mesmo pool usado pelo kernel.
- Ambos possuem CR3, páginas de código e de dados, quadros salvos e ciclo de
  vida distintos. O escalonador usa round-robin cooperativo no idle.
- Execução: consulta os ticks PIT reais do kernel pela syscall nativa
  WX_NATIVE_CLOCK_TICKS=3, salva a amostra na página própria de usuário,
  incrementa seu contador e cede por WX_NATIVE_YIELD=2.
- Retomada: wx_resume_user restabelece o ponto posterior ao INT 80h,
  com CR3 e registradores do serviço. A cada quantum, ele verifica
  sua flag de encerramento e consulta novamente o relógio.
- Ocioso: o shell concede um quantum ao serviço após 10 ticks PIT
  sem executar um busy-loop no kernel; caso contrário, usa HLT.
- Stop: envia a flag, espera WX_NATIVE_EXIT (código 0), publica STOPPED.
- Restart: reutiliza a mesma reserva de memória física/tabelas de página,
  reconstroi o pequeno programa CPL3 e incrementa a geração. O número
  de novas páginas consumidas por reinício é zero. As páginas físicas
  ficam reservadas ao slot até que o kernel seja desligado.

Não é um relógio de tempo civil, sincronização NTP nem serviço de rede.
É um monitor de ticks reais com um consumidor legítimo de syscall.

## Terminal e processos observáveis

    ps
    services
    svc start clockd
    svc stop clockd
    svc restart clockd
    svc stop memd
    svc start memd
    svc restart memd

A saída de ps usa o estado corrente do processo CPL3 real, incluindo PID,
CR3 e contador de quanta; ela não mostra a shell CPL0 como se fosse um
processo CPL3. Ao encerrar o serviço, seu PID deixa de constar em ps.
Services mostra estado RUNNING/STOPPED/FAILED e geração de instância.

Há exatamente dois serviços reais nesta fase: clockd e memd. Não existem
linhas fictícias para WinLogon, Task Scheduler, WMI ou antivírus.

## Teste de integração

O arquivo tests/runners/test_console_wrapper_fls_candidate.py aceita
--check-services e exige:

1. Autoinicialização da instância CPL3 quando WINUX_SERVICE_PREVIEW
   está definido na entrada do boot experimental.
2. Presença de PID 401 em ps e estado RUNNING em services.
3. Aumento de quanta e amostras PIT durante períodos ociosos.
4. STOP retirando o processo e publicando STOPPED.
5. RESTART com geração aumentada e novas amostras PIT.
6. Segunda parada sem status pendente.
7. Negativa de START duplicado/STOP repetido, CR3 e páginas livres
   preservados após restart.
8. PID 402 e CR3 diferente do clockd, quanta e total de páginas livres
   observados no próprio memd, parada, reinício e rejeição de nome desconhecido.
9. Regressões de memória, DLL, console, PCI/DMA, heap e CPL3 anteriores
   passando no mesmo boot QEMU.

A execução com os dois serviços passou em 45 grupos QEMU, e a validação
conjunta com FAT16 e desmontagem do volume passou em 47 grupos. O resultado final e o hash da ISO
ficam nos arquivos build/console-foundations-p4-test.json e
build/foundation-preview/foundation-preview-build.json.

Comando de reprodução:

    .\scripts\build\verify_foundations.ps1 -Python "C:\Program Files\Python310\python.exe"

Resultados: build/console-foundations-p4-test.json e serial log.

## Correção associada ao pool de memória

O pool_test original pressupunha que todas as páginas estivessem livres.
Isso era aceitável quando não havia processos residentes e passou a ser
incorreto com clockd. Agora mede as páginas livres na entrada, testa
esgotamento e dupla liberação somente dessas páginas, e exige o mesmo
total livre na saída. Não toca quadros possuídos pelo processo CPL3.

## Limitações pendentes

Existem dois slots explícitos com estados e endereços independentes,
mas ainda não um Service Control Manager geral. Faltam processos arbitrários,
supervisão e reinício automático após
falhas, prioridade, IPC, jobs, segurança e tokens, autenticação,
sessões, VFS persistente, chamadas bloqueantes, preempção por timer,
SMP e integração com drivers Windows SYS. O próximo serviço só deve
ser registrado quando executar trabalho implementado e comprovado.

O kernel histórico e as ISOs estáveis não são substituídos por esta ISO.
