# Estado público do projeto

O Moment é um aplicativo Linux nativo em desenvolvimento. Este repositório contém somente código original, documentação funcional e testes do projeto.

Já implementado: Library local com SQLite e player, captura Wayland via XDG ScreenCast/PipeWire, replay buffer, exportação MP4, screenshot PNG, áudio opcional, preferências persistentes, bandeja do sistema, popup próprio, atalhos globais F8/F9 via XDG GlobalShortcuts e detecção local de processos de jogos com perfis persistentes. Os perfis podem iniciar/parar gravação contínua automaticamente quando habilitados.

Em desenvolvimento: ampliar a cobertura de launchers/compositores na detecção, eventos automáticos por jogo, editor de vídeo não destrutivo, faixas de áudio separadas, backend X11, compartilhamento e recursos de nuvem.

CTest também verifica perfis com uma árvore `/proc` sintética e confirma que os overrides do jogo não alteram preferências globais. A instalação local e os comandos de build estão descritos no README.

## Pesquisa de referência

A investigação REA da aplicação de referência foi mantida fora do repositório público. Ela serviu para separar comportamento observado, hipótese e requisitos independentes; nenhum binário, código extraído ou recurso proprietário é necessário para construir o Moment.
