# Atalhos globais e captura em segundo plano

## Estado atual

F8 solicita salvar o replay recente; F9 solicita um screenshot PNG. As teclas preferidas são enviadas ao portal `org.freedesktop.portal.GlobalShortcuts`, que registra as ações no desktop, independentemente do foco da janela. O app ID continua `io.moment.Moment`; os IDs das ações continuam `save-replay` e `save-screenshot` para preservar escolhas anteriores. Combinações alteradas pelo usuário no sistema têm prioridade sobre os defaults.

A implementação usa a API pública [XDG GlobalShortcuts](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html), verificada nesta revisão. O protocolo permite uma tentativa de BindShortcuts por sessão, informa alterações por ShortcutsChanged e separa configuração histórica de ativação efetiva.

## Fluxo implementado

1. Ao iniciar, sem depender de abrir Preferences ou focar a janela, criar a sessão e chamar **BindShortcuts** com as duas ações.
2. O desktop restaura escolhas anteriores ou apresenta autorização/configuração quando necessário. O app só declara ativo um atalho aceito com descrição de combinação não vazia.
3. O Moment permanece inscrito em Activated com a janela visível, minimizada ou oculta na bandeja. Os callbacks chamam o gravador diretamente; não usam QML Shortcut, eventos de teclado da janela nem verificam foco.
4. F8 usa a duração de replay configurada. F9 salva o frame mais recente da fonte de captura autorizada. Os arquivos finalizados entram na Library e os sinais existentes disparam o popup próprio.
5. ShortcutsChanged atualiza as combinações ou desativa apenas a ação modificada, sem apagar a outra ação.
6. Se o serviço do portal desaparecer, invalidar o estado ativo. Quando ele voltar, registrar uma sessão nova automaticamente. Respostas de métodos de sessões anteriores são ignoradas por geração.
7. Se o portal fechar uma sessão sem reiniciar o serviço, o estado deixa de indicar ativo e Preferences permite reconectar. Uma tentativa cancelada/parcial também usa uma sessão nova ao tentar novamente, respeitando o limite de BindShortcuts.
8. Ao destruir o controlador, fechar sua sessão no portal. Fechar a janela para a bandeja não destrói o controlador nem o gravador.

A chamada ListShortcuts foi removida do caminho atual: consultar histórico não é necessário para enviar os mesmos IDs estáveis ao BindShortcuts. Nas versões anteriores, ListShortcuts era usado antes de BindShortcuts; o novo caminho reduz uma etapa e configura também a primeira utilização automaticamente.

As respostas Request::Response são assinadas antes de cada chamada, conforme [protocolo de Requests](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html), preservando a correção da corrida anteriormente identificada.

## Uso

- Iniciar o Moment e autorizar a fonte de captura se o sistema solicitar. Selecionar um **monitor/tela** para que a captura acompanhe o conteúdo exibido ao alternar entre programas; selecionar uma janela restringe a fonte àquela janela.
- Com replay ativo, minimizar ou fechar a janela para a bandeja. F8 salva o replay; F9 salva o screenshot, sem exigir que Moment recupere foco.
- **Quit Moment** encerra o processo e seus atalhos. Não há captura depois de sair do programa.
- O portal pode solicitar autorização ou oferecer combinações diferentes se houver conflito. A UI exibe as combinações efetivamente aceitas.

## Validação desta revisão

Build concluída e **9/9 targets CTest aprovados**.

- `global-shortcuts-test`: portal falso em barramento D-Bus isolado. Verifica restauração de escolhas, configuração automática inicial, teclas não atribuídas, recebimento com janela oculta e outra janela focada, retry após cancelamento, alterações parciais, reinício do serviço e encerramento de sessão.
- `background-capture-test`: usa GlobalShortcuts e ScreenRecorder reais com portal falso, banco/configurações temporários e frames sintéticos. Com a janela oculta e outra janela focada, recebe as duas ações, salva PNG e MP4, importa dois itens, decodifica frames do MP4 e verifica que o replay continua ativo. Não captura desktop ou microfone.
- Sonda KDE real sem janela: `LIVE KDE PORTAL: replay=1 screenshot=1 key=F8 screenshotKey=F9 result=0`. Aciona apenas as duas ações do Moment via `org.kde.kglobalaccel.Component.invokeShortcut`; não simula teclas físicas, não grava mídia e não aciona outros programas. O fonte fica em `tests/manual_global_shortcuts_probe.cpp`; a execução manual não pertence ao CTest.

```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Esses testes demonstram entrega de ações sem foco e persistência dos resultados em ambiente controlado. Ainda falta documentar pressão física das teclas durante gameplay real. Não foi validada compatibilidade com todos os jogos ou compositores. O teste de foco usa duas janelas offscreen no mesmo processo; a sonda real testa o despacho do KDE sem janela.

## Limites

A captura é da fonte previamente autorizada, não da janela com foco de forma automática. Captura de tela precisa estar ativa e o replay precisa acumular frames antes de salvar. Reconectar o portal de atalhos não restaura por si só uma sessão ScreenCast que o sistema tenha encerrado; nesse caso, Capture permite tentar novamente. O sistema pode inibir atalhos em contextos protegidos/bloqueados. Não se intercepta teclado fora das APIs autorizadas do desktop.

X11 ainda não tem backend próprio; a disponibilidade em outros desktops depende do portal. Não há detecção automática de jogos, teclas por jogo, PTT ou bookmarks neste incremento.

## Relação com a referência

A investigação estática do Medal identificou default built-in `clip`/`keyboard`/`F8`/`short_press` e o caminho de salvar clipes (Evidence `ev_84638…`, `ev_6f7de…`, `ev_d93603…`, `ev_811ac…`; relatórios `../medal_reverse_engineering/04_RECORDING_ENGINE.md` e `05_CLIP_SAVE_TRACE.md`). O registro Linux via portal e o screenshot são implementações próprias. Não há equivalência ampla validada com o Medal.

Ver também [bandeja e segundo plano](FEATURE_SYSTEM_TRAY.md) e [popup próprio](FEATURE_CAPTURE_POPUP.md).
