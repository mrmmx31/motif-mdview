# Leitor Markdown em C + Motif

**Versão de desenvolvimento 0.1.0. Interface compilada no Debian 13 x86_64; teste gráfico ainda pendente.**

Projeto independente, sem vínculo com Arquivo Pessoal, Forja ou outros aplicativos. A proposta é abrir arquivos Markdown locais em uma janela pequena, sem VS Code, navegador, servidor local ou motor HTML.

## Estado desta entrega

- Núcleo de leitura e interpretação em C compilado e testado.
- Interface escrita em C usando Motif para a janela, seletor de arquivos e rolagem, e Xft para desenhar texto UTF-8.
- Apenas **Abrir** e **RAW** na barra. A leitura começa no modo formatado.
- Interface compilada e vinculada no Debian 13 x86_64, com GCC 14.2, Motif 2.3.8, X11, Xt e Xft. Foi corrigido o nome da função de encerramento para `XtDestroyApplicationContext`. A abertura e a aparência da janela ainda precisam do teste do usuário.
- Este pacote contém fontes, não executável, instalador ou `.deb`. A compilação gera `build/mdv`; não é uma entrega pronta para instalação.
- O empacotamento e a associação efetiva de `.md` ficam para depois da compilação e do teste do aplicativo. O arquivo `.desktop` é apenas uma proposta, não foi instalado.

## Compilar em Linux com X11

Requisitos: compilador C11, Make, pkg-config, Motif, X11/Xt e Xft com cabeçalhos de desenvolvimento. Em Debian/Ubuntu, os nomes usuais dos pacotes de desenvolvimento são:

- `build-essential`
- `pkg-config`
- `libmotif-dev`
- `libx11-dev`
- `libxt-dev`
- `libxft-dev`

Uma fonte DejaVu Sans/DejaVu Sans Mono é recomendada. O programa procura essas famílias por Fontconfig, usado pelo Xft. Em sessão Wayland, será necessário XWayland. Não há versão nativa Windows, macOS ou Wayland nesta entrega.

Nenhum desses pacotes foi instalado automaticamente. Depois de os requisitos estarem disponíveis:

```sh
make
./build/mdv examples/exemplo.md
```

Ou, para abrir somente a janela e escolher um arquivo:

```sh
./build/mdv
```

Arquivos com espaços funcionam como um único argumento entre aspas:

```sh
./build/mdv -- "/caminho/Minhas notas.md"
```

`make` não instala o programa, não muda associações e não escreve nos documentos. A compilação gráfica foi confirmada na máquina de destino. O teste visual, os callbacks e o comportamento da janela ainda precisam de validação.

## Modo formatado e RAW

- Formatado é o estado inicial.
- RAW exibe a marcação sem interpretar Markdown, usando fonte monoespaçada.
- A alternância relê o arquivo e volta ao início. Portanto, mudanças externas no arquivo aparecem ao alternar.
- RAW não é um editor hexadecimal: BOM UTF-8 e CRLF são normalizados para exibição, controles são tornados inofensivos e UTF-8 inválido aparece como caractere de substituição. Nada é gravado no arquivo.
- RAW mantém tabulações no texto e as apresenta com largura fixa de quatro espaços.
- Selecionar outro arquivo mantém o estado do botão RAW escolhido pelo usuário.

## Markdown coberto pelo núcleo

É um subconjunto deliberado, **não um parser CommonMark completo**.

- Títulos ATX de `#` a `######` e títulos Setext simples.
- Negrito com `**` ou `__`; itálico com `*` ou `_`, em construções simples.
- Código inline com delimitadores de crases e blocos cercados por crases ou tils.
- Listas simples com `-`, `+` e `*`; numeração é preservada como texto.
- Primeiro nível de citação com `>` e separadores horizontais.
- Links simples: rótulo e destino ficam visíveis. Não são abertos nem acessados.
- Imagens: somente descrição e caminho ficam visíveis, sem carregar a imagem.
- Texto UTF-8, incluindo português; caracteres malformados são substituídos.

As quebras de linha da origem são preservadas; a janela faz quebra visual conforme a largura. Não há composição completa de parágrafos Markdown. Estilos sobrepostos complexos podem ser simplificados, por exemplo negrito + itálico, e os níveis de título têm prioridade na escolha da fonte.

## Limites conhecidos

- Sem renderização de imagens, tabelas diagramadas, HTML, Mermaid, LaTeX, fórmulas, notas de rodapé ou links de referência.
- Sem interpretação de tarefas além do texto `[ ]` e `[x]`.
- Sem edição, seleção/cópia no painel desenhado, busca, impressão, abas ou navegação por links nesta etapa.
- Sem downloads, execução de código, JavaScript, sincronização, banco de dados, histórico ou monitoramento de pastas.
- A renderização Xft usa glifos diretos. Emoji, fontes ausentes, composição de escritas complexas e texto bidirecional não foram validados.
- Até 8 MiB por arquivo, 200.000 linhas de origem, 200.000 trechos formatados e 500.000 linhas visuais. O primeiro limite atingido encerra a leitura com erro.
- O programa não é um visualizador de arquivos gigantes. Há buffers proporcionais ao documento; não há promessa de uso constante de RAM.
- Consumo de memória e desempenho da janela **não foram medidos**. A arquitetura evita um motor de navegador, mas isso não substitui medição no computador de destino.

## Testes disponíveis

Os testes do núcleo não precisam de Motif:

```sh
make test
make sanitize
```

`make test` requer Python 3 apenas para nove testes de integração de arquivos. A aplicação não usa Python.

Na máquina de destino, `make sanitize` passou com AddressSanitizer, UndefinedBehaviorSanitizer e detecção de vazamentos ativada (`detect_leaks=1`), sem diagnósticos nessa bateria. O primeiro ambiente havia exigido `detect_leaks=0`; esse limite não ocorreu nesta execução.

Isso cobre somente o núcleo e as entradas exercitadas, **não a interface nem uma garantia geral de ausência de falhas ou vazamentos**. Leia `VALIDACAO.md` para resultados e roteiro gráfico pendente.

## Organização

- `src/md_document.c/.h`: núcleo e representação de texto/trechos formatados.
- `src/mdv_motif.c`: janela Motif, desenho Xft e alternância RAW.
- `src/md_core_cli.c`: utilitário de teste do núcleo, não o aplicativo final.
- `tests/`: verificações automatizadas.
- `examples/exemplo.md`: documento para a primeira avaliação.
- `packaging/mdv.desktop`: candidato à futura integração ao menu e abertura de `.md`.

O nome `mdv` é provisório. A futura instalação para vários usuários poderá usar binário e lançador do sistema, respeitando permissões dos arquivos de cada usuário, sem base compartilhada de documentos. Isso ainda não foi implementado nem instalado.
