# Memória e E/S

O projeto é uma MIC-1 de 32 bits com I-cache implementada e D-cache planejada. O simulador usa uma RAM de 4096 palavras, com endereços expressos em **índices de palavras**. A CLI carrega uma imagem `.bin` a partir do endereço zero.

## Mapa atual

| Endereço        | Uso                                                |
| --------------- | -------------------------------------------------- |
| `0x000`–`0xFFC` | RAM para código, dados e pilha                     |
| `0xFFD`         | Escrita encerra a execução (`HALTED`)              |
| `0xFFE`         | Escrita emite o byte baixo como caractere          |
| `0xFFF`         | Escrita emite um inteiro com sinal (`PROGRAM_OUT`) |

`SP` começa em `0xFFC`; o primeiro `PUSH` ou `CALL` escreve em `0xFFB`. Os três endereços de E/S não armazenam dados. Leituras desses endereços e acessos além da RAM são rejeitados. O carregador rejeita imagens maiores que 4096 palavras ou cujo tamanho não seja múltiplo de quatro bytes.

## Latência atual

`Core` inicia com latência de dois ciclos para uma leitura ou escrita que acessa a RAM, inclusive para preencher uma linha da I-cache. Um acerto na I-cache usa um ciclo. O núcleo aceita alteração da latência da RAM por `Core::setMemoryLatency(unsigned)`, mas a CLI e a GUI ainda não oferecem esse ajuste. O método não valida o argumento; o valor zero não deve ser usado. Enquanto uma transação está pendente, o microprograma sustenta o pedido até a resposta. A Memória de Controle, de 256 palavras de 36 bits, é separada da RAM e não usa esse mecanismo.

## Cache de instruções

A I-cache é de mapeamento direto, bloqueante, com 16 linhas de quatro palavras. Para um endereço `a`, `block = floor(a / 4)`, `index = block mod 16`, `tag = floor(block / 16)` e `offset = a mod 4`. O bit `IF` da microinstrução envia apenas as leituras de instrução para a I-cache; leituras e escritas de dados continuam acessando a RAM diretamente. Um hit entrega a palavra no ciclo seguinte ao pedido, enquanto um miss preenche a linha após a latência configurada da RAM. Cada preenchimento lê as quatro palavras da linha; essa latência é modelada por transação de linha, sem custo adicional por palavra.

A linha que contém `0xFFD`–`0xFFF` fica fora da I-cache, inclusive `0xFFC`. Buscas válidas nessa linha passam diretamente pela RAM e são indicadas como acesso sem cache, sem alterar os contadores de hit ou miss. A implementação assume que programas não alteram o próprio código. Uma nova carga ou reset da sessão cria uma I-cache vazia; voltar um ciclo restaura suas linhas e seus contadores. A GUI mostra acertos, falhas e o último tipo de acesso à I-cache.

## Extensões planejadas

A D-cache atenderá leituras e escritas de dados, com escrita direta na RAM e sem alocação em miss de escrita. A linha que contém `0xFFD`–`0xFFF` também ficará fora da D-cache.

Quando a D-cache for implementada, as caches compartilharão uma porta da RAM. O árbitro dará prioridade à D-cache quando ambas solicitarem acesso no mesmo ciclo. Também estão planejados contadores de acessos e ciclos de espera.
