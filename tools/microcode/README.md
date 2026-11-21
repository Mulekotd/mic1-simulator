# Microasm

O arquivo [`mic1.masm`](mic1.masm) contém o microprograma da MIC-1. Cada linha de código começa com um endereço decimal de `0` a `255`, seguido de `:` e de cláusulas separadas por `;`. São aceitos `rd`, `wr`, `ifetch`, atribuições (`destino:=expressão`), `goto N` e `if n|z then goto N`. Expressões usam registradores, `mbr`, `+`, `band(...)`, `inv(...)`, `lshift(...)` e `rshift(...)`. As linhas `; @phase Busca`, `; @phase Identifica` e `; @phase Executa` definem a fase exibida na listagem; linhas de comentário comuns também começam com `;`.

Na raiz do projeto:

```sh
python3 tools/microcode/microasm.py tools/microcode/mic1.masm -o ctrl.bin -l ctrl.mlst
python3 -m tools.microcode.build_rom --check
```

`ctrl.bin` contém até 256 palavras de controle de **5 bytes little-endian**; os quatro bits altos do quinto byte são zero e os endereços omitidos recebem zero. `ctrl.mlst` relaciona endereço, palavra de 36 bits, fase, linha e fonte. `ifetch` define o bit IF e exige `rd`.

## Como o montador codifica uma linha

Por exemplo, `0: mar:=pc; rd; ifetch;` usa `PC` na porta B para atualizar `MAR`, inicia uma leitura e marca a operação como busca de instrução. O `microasm.py` converte cada cláusula em campos da palavra de controle e os reúne com deslocamentos e OR. Nesse caso, a palavra é `0x10C004001`, que também aparece na posição 0 de [`Mic1Rom.hpp`](../../simulator/src/generated/Mic1Rom.hpp).

| Bits | Campo | Origem no código simbólico |
| --- | --- | --- |
| 35 | AMUX | Seleciona registrador A (`0`) ou MBR (`1`) |
| 34–33 | COND | Sem desvio, `if n`, `if z` ou `goto` |
| 32–31 | ALU | Soma, AND, cópia de A ou inversão de A |
| 30–29 | SH | Sem shift, `rshift` ou `lshift` |
| 28–24 | MBR, MAR, RD, WR, ENC | Escritas em MBR/MAR/C e pedidos de memória |
| 23–19, 18–14, 13–9 | C, B, A | Índices dos registradores |
| 8–1 | ADDR | Destino de `goto` ou desvio condicional |
| 0 | IF | Marca `ifetch` |

`build_rom.py` usa o mesmo `assemble()` para gerar o vetor C++ `microasm` em `Mic1Rom.hpp`. Cada literal hexadecimal representa **uma** microinstrução de 36 bits, e sua posição no vetor é o endereço lido pelo MPC. O sufixo `ULL` dá ao literal o tipo inteiro de 64 bits usado para armazenar esses 36 bits. O arquivo `ctrl.bin` serializa as mesmas palavras em cinco bytes little-endian; o vetor C++ contém valores numéricos e não depende da ordem dos bytes do arquivo.

O montador rejeita registradores desconhecidos, endereços repetidos ou fora de 0–255, `rd` com `wr`, `ifetch` sem `rd`, múltiplos desvios e usos incompatíveis dos barramentos B e C. As mensagens de erro incluem a linha da fonte.
