# Capturas de tela no Moment

## Estado

Implementado como uma fatia local: o Moment guarda o frame mais recente recebido pela sessão ScreenCast autorizada, grava PNG em `~/Pictures/Moment` (ou no diretório equivalente fornecido pelo XDG) e adiciona a imagem à Library. A tela Capture também oferece um botão; a preferência de atalho global registra `F9` como combinação preferida para screenshot. O usuário pode receber outro binding do portal.

O fluxo depende de a captura já estar ativa e ter entregado ao menos um frame. Screenshot não inicia uma nova sessão nem contorna o consentimento do portal. Uma captura de imagem salva o frame mais recente e não é uma fotografia garantidamente simultânea à tecla.

## Comportamento no Medal observado

- A documentação pública do Medal descreve um atalho para screenshots e armazenamento de screenshots na biblioteca. Consulte [Getting Started](https://support.medal.tv/support/solutions/articles/48000959661/) e [Medal feature list](https://medal.tv/blog/posts/medal-tv-feature-list-pc-game-clipping-editing-and-easy-sharing).
- No `app.asar` analisado, o módulo renderer de hotkey contém a ação literal `screenshot`, entre outras ações. Isso é evidência estática do vocabulário do módulo; não prova o caminho executado, atalho padrão, formato nem gravação no arquivo.
- A investigação REA selecionou `renderer-screenshot.js` e `renderer-screenshotUtils.js`, mas o recorte não reconstruiu um fluxo completo ou runtime. Não atribuímos ao Medal o diretório/formato usados pelo Moment.

## Fluxo próprio do Moment

1. `ScreenRecorder` recebe frames do stream do portal e mantém a imagem mais recente.
2. O botão Capture ou o evento `save-screenshot` chama `saveScreenshot()`.
3. A imagem é gravada em PNG com nome único.
4. O arquivo é importado como item de imagem da Library; a reprodução apresenta a imagem sem controles de vídeo.
5. O atalho é pedido ao XDG GlobalShortcuts junto do atalho de replay, com `F9` como preferência inicial.

## Verificação e diferenças

- Build concluído e os cinco targets do CTest passaram em 27/10/2026.
- `screen-recorder-test` injeta um frame sintético, aciona o salvamento, decodifica o PNG, verifica a entrada de biblioteca e o papel `isImage`.
- O teste é sintético. O registro do atalho `F9` no portal e sua ativação real não foram verificados nesta execução.
- O usuário relatou que a permissão de tela agora só foi solicitada uma vez e que o app voltou funcionando após fechar e abrir. Isso valida a experiência relatada de persistência do consentimento no ambiente do usuário; não demonstra a ativação da nova tecla de screenshot.
- Ainda não há captura de screenshot por frame independente da sessão ScreenCast, área recortada, seletor de alvo, cursor configurável, captura de múltiplos monitores ou comparação visual com o Medal.

## Referências de implementação

- `src/recording/screen_recorder.cpp`: retenção e gravação do frame atual.
- `src/recording/global_shortcuts.cpp`: binding preferido F9 e dispatch do evento.
- `src/library/clip_library_model.cpp` e `qml/Main.qml`: classificar e exibir imagens.
- `tests/screen_recorder_test.cpp`: validação sintética do PNG e da Library.
