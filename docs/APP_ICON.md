# Ícone do Moment

O desenho original em `packaging/io.moment.Moment.svg` usa formas vetoriais próprias, sem fontes, marcas ou recursos de terceiros. O símbolo geométrico M verde é compartilhado pela janela, bandeja e cabeçalho da interface.

O ícone é incorporado ao executável em `:/icons/io.moment.Moment.svg`. `QApplication::setWindowIcon` define o ícone da aplicação; o lançador usa `Icon=io.moment.Moment` e o desktop ID existente `io.moment.Moment` foi mantido. Isso preserva a identidade usada pelos portais e pelos atalhos. CMake instala o SVG em `share/icons/hicolor/scalable/apps` junto com o lançador.

Validação: compilação concluída, `desktop-file-validate` sem erros e teste `main-window-visual-test` aprovado. A interface real foi renderizada com dados sintéticos e capturada com `QQuickWindow::grabWindow()` em `artifacts/icon-refresh/`. `moment-brand-window.png` é um recorte desse PNG, não uma simulação da barra de tarefas. O resultado no painel do usuário ainda não foi observado pelo agente.

Nesta instalação local, foram atualizados o lançador, o executável em `~/.local/bin` e os ícones hicolor (SVG e PNG de 16 a 256 px). O cache de aplicativos KDE foi reconstruído. Nenhum processo em execução foi encerrado; sair por **Quit Moment** e reabrir carrega o ícone no processo novo.
