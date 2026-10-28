# Estado público do projeto

O Moment é um aplicativo Linux nativo em desenvolvimento. Este repositório contém somente código original, documentação funcional e testes do projeto.

Já implementado e testado: Library local com SQLite e player, captura Wayland via XDG ScreenCast/PipeWire, replay buffer, exportação MP4, screenshot PNG, áudio opcional, preferências persistentes, bandeja do sistema, popup próprio e atalhos globais F8/F9 via XDG GlobalShortcuts.

Em desenvolvimento: editor de vídeo não destrutivo, seleção de jogos/processos, faixas de áudio separadas, backend X11, compartilhamento e recursos de nuvem.

O teste completo atual usa CTest e cobre nove alvos, incluindo ativação de atalhos sem foco com frames sintéticos, sem acessar gravações pessoais. A instalação local e os comandos de build estão descritos no README.

## Pesquisa de referência

A investigação REA da aplicação de referência foi mantida fora do repositório público. Ela serviu para separar comportamento observado, hipótese e requisitos independentes; nenhum binário, código extraído ou recurso proprietário é necessário para construir o Moment.
