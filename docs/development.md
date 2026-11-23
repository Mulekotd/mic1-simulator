# Estado e próximos passos

## Estado atual

O projeto é um simulador de MIC-1 de 32 bits com I-cache implementada e D-cache planejada. O montador C++ produz imagens `.bin` e listagens `.lst`; o microasm Python gera a ROM de controle. O núcleo já executa as 23 instruções da ISA, com RAM de 4096 palavras, E/S mapeada e traço opcional por ciclo. A GUI Raylib usa um modelo 3D, efeitos sonoros e um painel com acertos, falhas e último acesso da I-cache. A CLI carrega um `.bin` diretamente e o executa até `HALT`, salvo se receber `--cycles N`. [`simulator/src/GUI.hpp`](../simulator/src/GUI.hpp) contém a janela, iniciada pelo mesmo executável com `--gui`. A sessão mantém o estado da CPU, da RAM e da I-cache para permitir voltar um microciclo; a janela continua aberta após `HALT` e permite digitar o caminho de outro programa. A CLI carrega e executa arquivos em uma sessão independente. O diretório da GUI separada e o canal TX/RX foram removidos.

O build oferece a conferência da ROM gerada com `BUILD_TESTING=ON`. Testes de componentes e CLI também são registrados quando seus arquivos existem em `simulator/tests/`.

## Próximas etapas

1. Ampliar a visualização de registradores, memória, pilha, microinstrução e listagens. A pilha será lida diretamente de `SP` e da RAM.
2. Aprimorar os controles e a apresentação da execução na GUI.
3. Expor a configuração da latência de memória e implementar D-cache, arbitragem e estatísticas adicionais, preservando os resultados funcionais dos exemplos.
4. Integrar à GUI os indicadores da futura D-cache e de ciclos de espera.
5. Finalizar a experiência da GUI, a documentação e as demonstrações de programas representativos.

A [ISA](isa.md), a [microarquitetura](microarch.md), o [modelo de memória](memory.md) e o [README](../README.md) descrevem o comportamento e os comandos atuais.
