# Instalação local para uso diário

O Moment está instalado para o usuário davivbr, sem privilégios administrativos.

- Executável ELF: `/home/davivbr/.local/bin/medal-linux` (mesmo conteúdo da build validada com 9/9 targets CTest).
- Lançador: `/home/davivbr/.local/share/applications/io.moment.Moment.desktop`.
- Ícones: `~/.local/share/icons/hicolor/`, nome `io.moment.Moment`.
- Barra principal KDE: painel 66, tela 0, widget de tarefas 69. Foi acrescentado apenas `applications:io.moment.Moment.desktop`; os lançadores anteriores e o painel da outra tela foram preservados.

O lançador usa caminho absoluto no Exec/TryExec para não depender do PATH da sessão gráfica. A configuração foi aplicada ao painel em execução pela API de scripting do Plasma, sem reiniciar o shell. O estado fixado foi conferido no widget e no arquivo persistente. O lançador passou em desktop-file-validate, e o executável instalado respondeu a --help sem iniciar captura.

Abrir pelo ícone fixado ou pesquisar Moment no menu. Fechar a janela mantém o processo e a captura autorizada na bandeja. Quit Moment encerra o aplicativo. Não foi configurado início automático no login.

Esta instalação usa as bibliotecas do host; não é um AppImage independente. O ID do aplicativo, diretório de dados e IDs dos atalhos não foram alterados. Novas compilações devem atualizar o executável instalado para aparecer no lançador.
