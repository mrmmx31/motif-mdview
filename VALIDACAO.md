# Validação de desenvolvimento

Data: 6 de novembro de 2026.
Ambiente de compilação e testes: Debian GNU/Linux 13 (trixie), Linux x86_64, GCC 14.2.0, Motif 2.3.8, X11 1.8.12, Xt 1.2.1 e Xft 2.3.6.

## Passou

1. Núcleo compilado com C11, `-Wall -Wextra -Wpedantic -Werror` e otimização `-O2`.
2. 20.031 verificações do núcleo, sendo 20.000 entradas pseudoaleatórias determinísticas. As demais cobrem Markdown básico, RAW, UTF-8, NUL, controles, entradas grandes e limites.
3. Nove verificações de integração: arquivo com espaços/caracteres especiais, ausência de escrita (hash antes/depois), arquivo somente leitura, caminho inexistente, diretório, FIFO sem bloqueio, NUL, controles e limite de tamanho.
4. A mesma bateria do núcleo passou com AddressSanitizer e UndefinedBehaviorSanitizer, com detecção de vazamentos ativada (`detect_leaks=1`), sem diagnósticos nessa execução.
5. O utilitário de teste `mdv-core` e o aplicativo gráfico `build/mdv` foram produzidos. Os executáveis não estão incluídos nesta distribuição de fontes.
6. A interface compilou e vinculou com `-Wall -Wextra -Wpedantic -Werror`. O primeiro `make` revelou a chamada inexistente `XtAppDestroyApplicationContext`; substituí-la pela API correta `XtDestroyApplicationContext` resolveu a falha. Todos os testes foram repetidos após a correção.
7. O executável respondeu a `--version` e `--help` sem abrir display, e rejeitou múltiplos arquivos com código 2. Todas as bibliotecas compartilhadas foram resolvidas.
8. Fontconfig encontrou DejaVu Sans e DejaVu Sans Mono. A renderização dessas fontes na janela ainda não foi validada.

O relatório não equivale a conformidade CommonMark nem a auditoria de segurança completa.

## Bloqueado ou não executado

- O aplicativo gráfico não foi aberto. Seus callbacks, layout, rolagem, fontes e aparência ainda precisam de validação real.
- A sessão do usuário é Plasma/Wayland com `DISPLAY=:1`, mas `xdpyinfo` não conseguiu abrir o display no contexto de execução. Nenhuma tentativa de contornar esse acesso foi feita. Nenhum teste visual é declarado como concluído.
- Os sanitizadores foram executados somente no núcleo, não na interface.
- Consumo de memória da janela, instalação, associação `.md`, uso entre usuários e pacote `.deb` não foram testados.

## Roteiro de primeira validação gráfica

Com o executável compilado, na sessão gráfica do usuário:

1. Abrir `examples/exemplo.md`. Conferir títulos, negrito, itálico, listas, citação e código.
2. Confirmar que RAW começa desligado. Ligá-lo e desligá-lo repetidas vezes; verificar retorno à marcação original e depois à formatação.
3. Abrir outro arquivo pelo botão Abrir. Cancelar uma nova seleção e verificar se o documento anterior permanece.
4. Abrir caminho inexistente, diretório e arquivo inválido. Confirmar erro legível sem perder o documento já aberto.
5. Alterar ou remover o arquivo externamente e usar RAW; verificar releitura/erro e preservação da tela anterior quando a leitura falhar.
6. Rolar com roda e barra até o fim; redimensionar a janela repetidamente e verificar linhas, quebras e fontes.
7. Conferir acentos em português, nomes com espaços, arquivo vazio e arquivo somente leitura.
8. Fechar pela decoração da janela e reabrir. Confirmar ausência de alterações nos arquivos e de configuração persistida.
9. Medir RAM com uma nota pequena, um documento típico do usuário e um documento maior. Medir a GUI completa, não apenas o parser.

## Próxima etapa

Executar o roteiro gráfico com o usuário, corrigir o que o teste apontar, receber a referência visual em PDF e só então empacotar para a distribuição/arquitetura confirmadas. O `.desktop` existente é rascunho, não muda aplicações padrão. A dependência exata do futuro `.deb` deve vir do binário compilado na distribuição-alvo.
