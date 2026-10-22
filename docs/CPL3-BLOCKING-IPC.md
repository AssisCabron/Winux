# IPC bloqueante CPL3 no Winux — contrato e limites

Data: 21/10/2026. Implementação apenas na Foundation Preview x86-64.

## Objetivo

Permitir que um programa CPL3 espere uma mensagem sem ficar consultando a
mailbox repetidamente. O IPC reutiliza a única mailbox de serviços,
wx_proc_current_irq, wx_process_table e a fila READY do Process Manager.
Não foi criado um segundo escalonador nem um mecanismo Windows LPC falso.

## Contrato nativo v1

A syscall WX_NATIVE_IPC_RECEIVE_WAIT=8 opera para o destinatário memd (PID
402), identificado pelo kernel; não aceita PID fornecido pelo próprio
convidado. Se já existe uma mensagem, consome e retorna seu valor em RAX.
Caso a fila esteja vazia, marca seu processo BLOCKED/IPC com deadline de
30 ticks PIT, salva os 15 GPRs + RIP/RSP/RFLAGS e retorna ao agendador.
O programa não é escolhido antes de ser notificado ou atingir o deadline.

Quando clockd (PID 401) envia uma mensagem pela syscall existente
WX_NATIVE_IPC_SEND=5, o kernel verifica a identidade do remetente,
a presença dos dois serviços e a caixa cheia. Se memd está BLOCKED/IPC,
o valor é colocado no RAX de sua continuação salva, a thread vai para
READY e a mailbox permanece vazia (entrega direta, sem duplicação).
Em outros casos, o valor ocupa o único slot da mailbox; novas mensagens
são rejeitadas com backpressure até consumo.

Se não houver remetente antes do prazo, wx_proc_wake_blocked() acorda o
destinatário e grava RAX=0 na sua continuação. Zero é reservado para
ausência/timeout, portanto mensagens zero não são aceitas. Um processo
bloqueado ainda é considerado vivo, não EXITED nem FAILED.

## Provas reais

Comando dentro do terminal experimental:

    ipc-wait-test

A rotina prova duas condições de ponta a ponta usando as chamadas reais
de usuário (INT 80h):

1. Reinicia memd com mailbox vazia; seu código CPL3 chama
   WX_NATIVE_IPC_RECEIVE_WAIT e fica BLOCKED. O código CPL3 de clockd
   realiza WX_NATIVE_IPC_SEND; o Process Manager acorda memd antes do
   deadline e entrega o valor em seu frame salvo, sem deixar cópia na fila.
   Ao retomar, memd salva o valor recebido em sua página CPL3.
2. Reinicia memd sem clockd; espera pelos ticks PIT reais e verifica que
   o timeout retorna zero, a caixa continua vazia e o processo sai de
   BLOCKED pelo mesmo Process Manager, sem novas páginas ocupadas.

Para repetir pelo host:

    .\scripts\build\verify_foundations.ps1

A regressão inclui --check-ipc-wait, além dos testes de clockd/memd, TID,
IRQ0, FAT16, PCI/DMA e interfaces NT que já existiam.

## Escopo e limitações

O contrato acima é usado apenas pelo par clockd/memd em uma CPU. O
estado BLOCKED e o deadline ainda pertencem ao PROCESSO no registro atual,
não a cada TID; portanto, bloquear uma thread num processo multithread
também impede que suas irmãs sejam selecionadas por essa política. Antes
de estender IPC/espera para aplicativos multithread, os estados de espera,
ready queues e deadlines precisam pertencer a wx_proc_thread. Ainda não
é Windows LPC/ALPC, pipe, socket, semaphore, wait-all, thread pool ou
sistema completo de objetos de espera. A mailbox tem capacidade 1, é
64-bit, não entrega descritores ou handles e não suporta transferências
entre processos arbitrários. O hardware PIT é o temporizador de testes
QEMU, não uma implementação APIC/SMP generalizada.

Para generalizar, será necessário evoluir a tabela de objetos já
existente com ownership, filas de espera por objeto, cancelamento e
timeouts por thread, sem reutilizar indiscriminadamente o ID do PID
como autorização de IPC.
