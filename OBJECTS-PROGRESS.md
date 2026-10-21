# Implementação original de objetos: primeira parte do pacote

## Integração 0.16

A ISO `build/winux-0.16-x64.iso` contém as sete rotas de objetos. SHA-256 e5b21253f22daf2da05e1476908c9ea67b142cfb4947ce6b6153c3a66f0eee84. O comando `nt-object-test` mapeia a NTDLL original, verifica bytes/números de serviço dos sete stubs e executa 47 chamadas por SYSCALL. Os registros são comparados aos probes Windows por `check_object_records.py`. QEMU passou nessa comparação e nas regressões de console, UD2, páginas somente leitura e NX. A execução VirtualBox é registrada separadamente por hash.

O namespace desse teste é uma fixture configurada explicitamente e removida no final; o caminho C:\Windows\System32 serve à comparação, não representa um filesystem implementado. O teste não chama as entradas das DLLs Microsoft e não libera a execução do CMD. Espera bloqueante, scheduler, armazenamento guest completo e isolamento continuam pendentes. Validação de faixas VM dos adapters ainda não tem caso integrado específico; os argumentos deste teste ficam na pilha do kernel.

VirtualBox também passou: `build/virtualbox-0.16-test.json`, 17 verificações, mesmo hash da ISO, VM Winux deixada em execução. QEMU: 34 verificações de console e três suites separadas para UD2/RO/NX. Seis EXEs próprios passaram novamente no Windows com identidade dos bytes verificada pela suite QEMU.

Nota de artefato: uma tentativa intermediária sobrescreveu o arquivo chamado `winux-0.15-x64.iso` com SHA-256 45988933c20ef8b8548180ebd17109c46719d0cb3dd819daa2272ceeeece32f2, sem validar essa imagem. Os registros históricos da antiga 0.15 não se aplicam a esse arquivo. Usar a 0.16 pelo hash acima.

As seções abaixo registram a evolução e os limites do núcleo; menções anteriores à ausência de integração foram superadas apenas nos cenários testados acima.

`src/nt_objects.h` implementa uma tabela de 64 handles com geração, fechamento, invalidação no reset do processo e aposentadoria do slot quando a geração se esgota. O estado de evento representa tipo, sinalização e máscara de acesso; operações negadas não modificam estado nem saída. Não usa código decompilado da Microsoft.

Eventos tipo 0 preservam sinalização após polling bem-sucedido; tipo 1 consome o sinal. O único modo de espera implementado é não alertável com timeout zero. Esperas bloqueantes, alertáveis e outros timeouts devolvem explicitamente NOT_SUPPORTED. A tabela exige chamadas serializadas; não contém scheduler nem sincronização para múltiplas CPUs.

## Validação realizada

O pacote agora também fornece o namespace limitado `\KnownDlls`/`KnownDllPath`: abertura absoluta e relativa, tipos distintos, direitos de travessia/consulta, nomes fixos ASCII com opção case-insensitive e consulta UTF-16 com tamanho em bytes. O destino é configurável antes de abrir handles; não acessa arquivos do host. Ainda não fornece sections de imagens, criação de objetos nomeados arbitrários, ACLs ou Unicode case folding geral.

A comparação atual inclui 12 registros de namespace, além dos 35 de eventos. Capacidades 38/39/40 bytes verificaram a fronteira do tamanho requerido no host. Testes adicionais verificam canários, tipo incorreto, root encerrado, link sobrevivendo ao fechamento do root, case-sensitive lookup e direito de travessia. `tests/nt_namespace_fixture.h` recebe o destino observado somente como configuração da fixture host; a ISO precisará de um caminho guest real.

`test_nt_objects_host.py` compila o mesmo header com Zig/Clang, avisos como erros, e compara cada registro com `build/nt-events-windows.json`. Os 35 registros coincidiram. O teste C adicional verifica direitos, ausência de efeitos em operação negada, handles antigos após reutilização/reset, esgotamento sem modificar saída, rejeição de handle malformado, limpeza e aposentadoria por limite de geração.

Registro por hash: `build/nt-objects-host-test.json`. Esses testes executam o núcleo original no host. Não passam pelo dispatcher NT, validação de ponteiros guest ou VirtualBox e não comprovam a nova funcionalidade na ISO.

## Integração ainda necessária

Atualização: `nt_object_api.h` foi conectado ao runtime e ao dispatcher, com sete assinaturas NT, validação de ponteiros e limpeza de handles no reset/retorno do programa. Os validadores incluem regiões VM comprometidas com permissões apropriadas, além das faixas já conhecidas do runtime. O kernel compilou para `build/BOOTX64-objects-check.EFI`, sem substituir a imagem em uso. O teste host de adapters cobre limites de ponteiros e falhas sem efeitos, com arena simulada; a validação das páginas VM e a transição SYSCALL ainda precisam de teste na VM.

O namespace guest não recebe destino fictício automaticamente: permanece indisponível até configurar um caminho sustentado pelo armazenamento. Fixtures host configuram seu próprio destino. A matriz de rotas indica presença no código, não sucesso na ISO antiga.

- Validar adapters/ciclo de vida na VM e ampliar direitos/atributos conforme os contratos; mapeamento genérico e ACLs ainda não implementados.
- Conectar o namespace limitado de diretórios/links ao armazenamento guest; ampliar objetos nomeados e segurança conforme os consumidores.
- Estado de threads e filas de espera/sinalização, timeouts e cancelamento; garantir ausência de notificações perdidas.
- Teste integrado das novas rotas usando stubs originais; atualizar a ISO e repetir as regressões.

A matriz NT distingue agora as novas rotas no código. A ISO 0.15 em uso ainda não contém essa implementação; o CMD original permanece bloqueado.
