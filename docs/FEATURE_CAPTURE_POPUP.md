# Popup de captura do Moment

## Comportamento e implementação

Após a finalização de replay, screenshot ou gravação, o Moment mostra uma janela própria no canto inferior direito durante 4,2 segundos. A janela principal pode estar oculta na bandeja. O card usa cores, tipografia e símbolo próprios; exibe o nome do arquivo e uma miniatura quando é screenshot. Novos eventos substituem o conteúdo e reiniciam a duração. Falhas de captura e replay ainda vazio também aparecem nessa janela.

`CapturePopup` usa QQuickView e `qml/CapturePopup.qml`, com fundo transparente, animação breve e barra de duração. A janela não aceita foco e é transparente a cliques. Nenhuma chamada a `QSystemTrayIcon::showMessage` ou ao serviço de notificações é feita.

No Wayland com LayerShellQt 6.6+, o popup usa uma superfície exclusiva dessa janela na camada overlay, ancorada embaixo à direita, sem reservar espaço e sem interatividade de teclado. A biblioteca já estava instalada no host; não foi necessário instalar ferramentas. As outras janelas do Moment mantêm sua integração normal. Em X11, usa janela acima das demais e a geometria disponível do monitor sob o cursor.

Os sinais `replayClipSaved`, `screenshotSaved` e `recordingSaved` abrem o popup apenas depois da conclusão da escrita. `operationFailed` e o atalho sem vídeo acumulado abrem a variante de aviso. Falhas de captura não abrem mais diálogo modal da janela principal.

## Verificação

- Build e CTest: 7/7 aprovados.
- `capture-popup-test`: carregamento QML, janela separada visível, flags de foco/entrada, substituição de conteúdo e ocultação automática. A imagem renderizada foi inspecionada em `build/moment-popup-preview.png`.
- O mesmo teste foi executado no Wayland real deste host, com LayerShellQt, e passou sem erros. Ele abre somente o popup, sem capturar tela ou áudio.
- O teste real do portal de atalhos usou `invokeShortcut` do componente Moment no KGlobalAccel e recebeu os dois eventos F8/F9. Não houve uma partida de jogo ou acionamento por tecla física nesta sessão.

## Limitações

Sobreposição e ancoragem em Wayland dependem de suporte do compositor a layer-shell. A dependência é opcional no build; em compositores sem esse protocolo ou builds sem LayerShellQt, uma janela Qt comum pode ter posição/sobreposição limitadas pelo compositor. Não foi validado jogo em fullscreen exclusivo. Isso não é uma injeção ou overlay dentro do processo de outro aplicativo.
