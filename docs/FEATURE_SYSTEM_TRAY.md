# Bandeja do sistema e captura em segundo plano

## Comportamento

- O ícone próprio do Moment aparece na área de status do desktop enquanto o processo está ativo.
- Fechar a janela principal oculta a janela e deixa o Moment e o replay rodando em segundo plano quando o desktop expõe a bandeja do sistema.
- Clicar no ícone, dar duplo clique ou selecionar **Open Moment** restaura e ativa a janela.
- **Quit Moment** encerra o aplicativo de verdade. Se uma sessão contínua estiver gravando, o app pede o fim da gravação, aguarda o MP4 e para o replay antes de sair.
- Em sessões sem bandeja disponível, fechar a janela encerra o processo para não deixá-lo escondido e inacessível.

O ícone não inicia o Moment no login. A pessoa inicia o app como antes; depois pode fechar a janela sem interromper a captura. Ele não tenta manter gravação ativa depois de **Quit Moment** ou do encerramento do processo/sistema.

## Implementação

`TrayController` usa `QSystemTrayIcon`/`QMenu` e um ícone desenhado pelo próprio projeto. A janela QML cancela o fechamento e se oculta quando a bandeja está disponível. `QApplication::setQuitOnLastWindowClosed(false)` mantém o event loop vivo. A ação Quit drena gravação contínua/export de replay antes de encerrar.

## Verificação e limitações

- Build CMake passou e a suíte atual passou 5/5.
- O smoke do app com QPA `offscreen` permaneceu em execução até o timeout planejado e não imprimiu erros. Esse plugin não fornece uma bandeja real.
- O host da execução não expôs uma janela de desktop para testar o clique/fechamento no painel KDE. O usuário ainda precisa confirmar que o ícone aparece, fechar a janela e reabri-la pelo ícone no desktop.
- Não há autostart no login, indicador dinâmico de gravação no menu ou opção “fechar ao sair” nas Preferences.
