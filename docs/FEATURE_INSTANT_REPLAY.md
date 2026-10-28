# Instant replay buffer — primeira implementação local

## Estado

Vertical inicial implementada e testada no host em 27/10/2026. A janela principal do Moment inicia uma sessão ScreenCast do portal XDG; o stream PipeWire entra por GStreamer e é passado ao Qt Multimedia, cuja saída alimenta o `QVideoSink`. A tela Capture mantém histórico em memória e exporta a duração escolhida (15–120 segundos, até o que está disponível no buffer) para MP4 na Library. A autorização persistente usa `persist_mode=2` e um restore token armazenado ao lado das configurações com permissões de arquivo somente para o usuário. O tamanho da janela de retenção e o comprimento de clipe persistem em `settings.ini`.

Isto não é equivalência validada com o Medal. A investigação do artefato Medal encontrou buffers temporais de vídeo e áudio e exportação por timestamps, mas a semântica de alguns campos nativos ainda está em aberto. O Moment implementa uma estratégia independente: JPEG por frame para vídeo e PCM por blocos para áudio, ambos na RAM e limitados por tempo e bytes.

## Caminho implementado

1. `Component.onCompleted` da janela chama `ScreenRecorder::startBuffering()` após o Moment abrir.
2. `WaylandScreenCast::start()` cria uma sessão ScreenCast, solicita uma fonte de monitor e pede persistência de autorização quando o portal suporta essa opção. Na primeira aprovação o portal pode mostrar o seletor; em aberturas seguintes, o Moment envia o token de restauração. Em 27/10/2026, o usuário confirmou que fechou e reabriu o app e a permissão foi solicitada somente uma vez. Isso é validação manual reportada pelo usuário para esse ambiente.
3. Cada frame entregue ao `QVideoSink` é encaminhado a `InstantReplay::captureFrame()`; se ativado, `QAudioSource` fornece buffers PCM em uma linha temporal monotônica correspondente ao replay.
4. `InstantReplay` limita a taxa ao perfil de 30/60 FPS, converte para JPEG num worker e mantém frames recentes pela janela configurada, com teto adicional de 128 MiB.
5. **Save last N sec** usa a preferência `replayClipSeconds`, seleciona frames por timestamp, decodifica-os em lotes e fornece `QVideoFrame`s ao `QMediaRecorder` de exportação.
6. O resultado MP4/H.264, opcionalmente com áudio AAC, é importado à biblioteca local depois que o arquivo final existe e não está vazio.

```mermaid
sequenceDiagram
    actor User
    participant UI as Moment Capture UI
    participant Recorder as ScreenRecorder
    participant Portal as XDG ScreenCast Portal
    participant PW as PipeWire / GStreamer
    participant Buffer as InstantReplay
    participant Encoder as Qt MP4 encoder
    participant Library as SQLite Library
    UI->>Recorder: startBuffering() on window open
    Recorder->>Portal: CreateSession / SelectSources(persist_mode=2, restore_token?) / Start
    Portal-->>Recorder: streams + new restore token
    Recorder->>Portal: OpenPipeWireRemote()
    Portal-->>PW: PipeWire FD + stream identity
    PW-->>Recorder: QVideoFrame from GStreamer appsink
    Recorder-->>Buffer: QVideoSink frame
    User-->>Recorder: Audio input enabled in Preferences (default off)
    Recorder->>Recorder: QAudioSource reads selected PCM source
    Recorder-->>Buffer: Audio buffers + monotonic timestamps
    Buffer->>Buffer: JPEG em worker, timestamp e trim temporal/bytes
    User->>UI: Save last N sec / atalho
    UI->>Recorder: saveReplayClip(configured duration)
    Recorder->>Buffer: snapshot por timestamp
    Buffer->>Encoder: frames via QVideoFrameInput; PCM via QAudioBufferInput
    Encoder-->>Buffer: recorder finalizado
    Buffer->>Library: importFile(MP4)
    Library-->>UI: clip aparece na Library
```

## Verificação

- `instant-replay-test`: injeta frames sintéticos, espera o buffer formar duração positiva, exporta MP4, abre o arquivo no player Qt e confirma que frames e duração são decodificados.
- `instant-replay-test`: segunda fixture injeta PCM sintético e confirma que o arquivo muxado produz buffers de áudio ao ser decodificado.
- `screen-recorder-test`: grava frames sintéticos em MP4 e confirma importação na Library.
- `recording-preferences-test`: agora cobre persistência e rejeição de duração do replay além de FPS/qualidade.
- Build `cmake --build build -j2`: passou.
- `ctest --test-dir build --output-on-failure`: 5/5 passaram.
- Smoke do app após o autostart: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 4s ./build/medal-linux --data-dir /tmp/moment-auto-replay-smoke`; QML/inicialização sem mensagens, processo encerrado pelo timeout planejado. Backend offscreen não confirma acesso ao portal.

## Limitações atuais

- O áudio é opcional e apenas uma fonte por vez; não há multi-track ou captura per-app. Os testes de áudio usam PCM sintético, ainda sem validação de microfone/monitor ao vivo ou drift A/V sob carga.
- Compressão intermediária é JPEG por frame; durante codificação em worker há somente um slot para frame pendente, portanto carga acima da capacidade pode descartar frames. Não há medição de FPS efetivo, CPU, latência ou qualidade visual.
- Teto de memória é global para o conjunto de JPEGs, não uma previsão exata de segundos. Em cenas complexas, o limite de bytes pode encurtar a janela antes do tempo configurado.
- Export depende do suporte de Qt Multimedia a MP4/H.264 no host. O teste confirma o plugin deste host, não todos os sistemas Linux.
- Não há seleção de jogo/janela, notificações ou recuperação automática após falha; não houve teste de gravação/replay simultâneos em portal real. O hotkey global do Moment está implementado separadamente pela API pública de GlobalShortcuts e ainda requer validação manual no desktop.
- A persistência da permissão foi reportada pelo usuário como funcionando entre execuções. Não repetimos essa validação independentemente nesta sessão. O recebimento contínuo de frames, geração de replay e acionamento por hotkey durante gameplay ainda precisam de teste manual integrado. X11 ainda não tem esse adapter.
- Diagnóstico de 27/10/2026: o log `qt.qpa.services ... Connection already associated with an application ID` coincide com registro duplicado do ID: Qt recebe `io.moment.Moment` via `setDesktopFileName`, e o Moment chamava `host.portal.Registry.Register` diretamente. Removemos o registro manual e mantivemos o ID de desktop para o Qt associar ao portal. `WaylandScreenCast::fail()` agora escreve o motivo completo no terminal; a próxima execução real ainda é necessária para confirmar se essa era também a causa do estado idle.
- O indicador distingue agora espera de permissão de captura parada. Se o portal entregar sessão mas o PipeWire não entregar nenhum frame em 8 segundos, o Moment encerra a tentativa com uma mensagem de erro específica; o primeiro frame recebido é registrado no terminal. Isso permite separar falha de autorização de falha de stream sem inferir que `PLAYING` significa vídeo chegando.
- Diagnóstico adicional: a sessão ScreenCast aparecia no barramento enquanto a UI permanecia aguardando, consistente com resposta do portal perdida. O cliente passou a assinar a resposta prevista antes da chamada e validar o caminho de retorno. O usuário confirmou que a tela de captura agora inicia e não volta a pedir autorização depois de reiniciar o Moment; duração efetiva e continuidade dos frames ainda precisam de observação registrada.
- Log de 27/10/2026 mostrou avisos de duração negativa no stream de áudio do MP4. O caminho anterior convertia cada início de buffer para milissegundos, arredondando blocos de áudio independentemente. O Moment agora preserva timestamps em microssegundos desde a origem amostrada e calcula PTS de clipe nessa unidade. A mudança compila; o MP4 real ainda precisa ser gerado e reproduzido para confirmar que os avisos desapareceram.
- O mesmo log mostra que `h264_nvenc` foi recusado para o formato de pixel reportado, após o que o Qt iniciou `libx264` por CPU. A gravação continuou; aceleração NVENC não foi validada neste host e a seleção de encoder ainda não é configurável na UI.
- Comprimento do clipe agora é configurável em Preferences e compartilhado pelo botão e hotkey. Se o buffer tiver menos dados que a duração escolhida, o clipe contém apenas o trecho disponível.

## Próxima validação valiosa

Em uma sessão Wayland real, abrir o Moment, autorizar/selecionar a tela no portal, esperar o contador avançar, salvar pelo atalho e reproduzir o arquivo resultante. Isso verifica a cadeia que os testes sintéticos ainda não cobrem. Depois, medir perda de frames/memória e fazer um spike com backend de segmentos codificados para evitar JPEG por frame.
