# Matriz de paridade funcional — Moment x comportamento de referência

Este documento é um backlog técnico verificável. “Implementado” significa que há código e testes locais para o comportamento do Moment; “Equivalência validada” exige comparação reproduzível com um comportamento de referência e não é usada apenas porque o projeto compila.

| Funcionalidade | Comportamento observável de referência | Componentes Moment investigados | Evidências existentes | Limitações | Requisitos Linux | Estado | Testes | Diferenças atuais |
|---|---|---|---|---|---|---|---|---|
| Inicialização e gerenciamento de processos | Aplicativo inicia interface, serviços de captura e estado persistente | `src/main.cpp`, bandeja, preferências | Código/testes do projeto | Não há comparação dinâmica completa | Qt 6, processo único com worker de captura | Implementado | CTest + execução manual | Sem supervisor/worker separado |
| Interface e navegação | Biblioteca, preferências, captura e estados de gravação acessíveis pela UI | `qml/Main.qml`, `qml/CapturePopup.qml` | Screenshot/teste visual local | Fluxos visuais não são equivalência validada | Qt Quick/QML | Implementado | `main-window-visual-test` | Identidade visual própria |
| Configurações e persistência | Preferências globais sobrevivem ao reinício e perfis por processo são guardados | `recording_preferences.*`, `GameMonitor` | Testes de preferências e perfil | Sem backup/migração avançada de esquema | `QSettings`/XDG | Implementado | `recording-preferences-test`, `game-monitor-test` | Sem perfil de áudio por jogo |
| Gravação contínua | Iniciar/parar uma sessão e catalogar o arquivo | `ScreenRecorder`, `GameMonitor`, portal, GStreamer/Qt Multimedia | `docs/FEATURE_CONTINUOUS_RECORDING.md`, `docs/FEATURE_GAME_PROFILES.md` | Auto-start/stop integrado, ainda sem validação com partida real; captura depende do compositor | XDG Portal, PipeWire, H.264/MP4 | Implementado | `screen-recorder-test`; detector isolado | Sem captura exclusiva por janela/jogo |
| Replay buffer | Manter janela temporal e salvar os segundos anteriores | `InstantReplay` | `docs/FEATURE_INSTANT_REPLAY.md` e testes sintéticos | Não há equivalência dinâmica com o buffer proprietário | Buffer em memória/segmentos, encoder | Implementado | `instant-replay-test` | Sem seleção automática de jogo |
| Atalhos globais | Ativar captura sem foco na janela | `global_shortcuts.*` | Portal GlobalShortcuts + testes | Limitações dependem do compositor | XDG GlobalShortcuts | Implementado | `global-shortcuts-test`, teste manual | Sem perfis específicos por jogo |
| Detecção automática de jogos | Reconhecer processo de jogo configurado e selecionar perfil | `GameMonitor` observa `/proc`, perfis por nome de executável | `docs/FEATURE_GAME_PROFILES.md`; teste com árvore `/proc` sintética | Identifica processo, não janela em foco; cobertura automática depende de caminho reconhecível | `/proc`, QSettings/JSON, portal de captura | Implementado | `game-monitor-test` + teste de override | Só cobre heurísticas Steam/Lutris/Heroic/GOG/games e cadastros manuais; sem eventos de gameplay |
| Captura automática de kills/vitórias | Gerar clip a partir de eventos de jogo | Ainda não implementado | Nenhuma evidência pública suficiente | Integração varia por jogo e pode exigir plugins | API de eventos/plugins opt-in | Não investigado | — | Não existe camada de eventos |
| Bookmarks durante partidas | Marcar instante sem necessariamente exportar imediatamente | Replay/Library não têm entidade bookmark | Nenhum teste | Sem definição de persistência | SQLite + timestamps monotônicos | Não investigado | — | Falta modelo e UI |
| Áudio separado por aplicativo | Selecionar/misturar fontes independentes | `ScreenRecorder` tem entrada PCM opcional | `docs/FEATURE_AUDIO.md` | Áudio real e sincronização ainda não validados | PipeWire nodes, mixagem por stream | Investigado parcialmente | Cobertura sintética parcial | Hoje não há faixas independentes |
| Microfone e jogo em faixas separadas | Preservar fontes em trilhas distintas | Captura PCM única | Documentação/teste parcial | Falta pipeline multifaixa/mux | PipeWire + GStreamer/FFmpeg | Investigado parcialmente | Sem teste integrado | Necessita modelo de tracks |
| Webcam sobreposta | Compor câmera sobre o vídeo | Nenhum componente | Nenhuma evidência | Sem decisão de layout/encode | PipeWire + compositor de frames | Não investigado | — | Ausente |
| Clipe ativado por voz | Comando de voz dispara salvamento | Nenhum componente | Nenhuma evidência | Privacidade e backend não definidos | Vosk/Whisper local opcional | Não investigado | — | Ausente |
| Configurações específicas por jogo | FPS, qualidade e janela/duração do replay por processo configurado | `GameMonitor` + overrides efetivos de `RecordingPreferences` | `docs/FEATURE_GAME_PROFILES.md`; teste de override e persistência | Não há perfil de áudio por jogo; mudanças de encoder aplicam-se às operações seguintes | QSettings/JSON, Qt, scanner local | Implementado | `game-monitor-test`, `recording-preferences-test` | Sem correspondência por janela ativa ou configuração de áudio dedicada |
| Salvamento/exportação | Finalizar contêiner e inserir na Library | `ScreenRecorder`, `ClipRepository` | Testes de MP4/SQLite | Formatos e falhas de disco ainda limitados | MP4/WebM, escrita atômica | Implementado | recorder/library tests | Sem fila robusta de exportação |
| Biblioteca de clipes | Listar, reproduzir e persistir metadados | `clip_repository.*`, `clip_library_model.*` | Testes de repositório/player | Sem tags avançadas | SQLite + Qt Multimedia | Implementado | `clip-library-tests`, `media-player-test` | Sem nuvem/feed |
| Editor de vídeo com timeline | Cortar/organizar/exportar clipes | Nenhum editor integrado | `docs/OPENCUT_FEASIBILITY.md` | OpenCut ainda não integrado | FFmpeg/GStreamer ou editor próprio | Investigado parcialmente | — | Em desenvolvimento |
| Notificação de clip salvo | Mostrar popup próprio após salvar | `capture_popup.*`, QML | Testes de popup | Sem comparação visual direta | Qt Quick overlay/layer-shell | Implementado | `capture-popup-test` | Visual próprio |
| Links para compartilhar | Gerar link e compartilhar arquivo | Nenhum backend | Nenhuma implementação | Serviços remotos não autorizados/investigados | Backend próprio ou exportação local | Não investigado | — | Ausente |
| Upload automático para nuvem | Enviar e acompanhar estado | Nenhum backend | Nenhuma implementação | Requer serviço próprio e autenticação | API HTTPS própria | Não investigado | — | Ausente |
| Conta e perfil | Login, perfil e preferências remotas | Nenhum backend | Nenhuma implementação | Não usar serviços privados do Medal | OAuth/backend próprio | Não investigado | — | Ausente |
| Feed social, amigos e comentários | Conteúdo social associado a clipes | Nenhum backend | Nenhuma implementação | Escopo de servidor ainda inexistente | Backend próprio, moderação e privacidade | Não investigado | — | Ausente |
| Atualização e manutenção | Distribuir versões e migrar dados | CMake/PKGBUILD | Pacote Arch local testado | Sem canal de atualização do app | AUR/release assinada | Investigado parcialmente | makepkg/CTest | Sem updater embutido |

## Ordem de desenvolvimento proposta

1. Validar auto-start/stop com uma sessão de jogo real autorizada e ampliar detecção para launchers/compositores que o usuário utiliza.
2. Bookmarks com persistência e exportação resiliente.
3. Pipeline de áudio com trilhas separadas (jogo/microfone).
4. Editor vertical de corte/exportação na Library.
5. Eventos específicos de jogo apenas via plugins/APIs opt-in.
6. Webcam, voz e recursos de compartilhamento somente após a base local estar estável.

## Critério de avanço

Cada linha deve ganhar: requisito independente, implementação original, teste automatizado quando possível, teste manual documentado e uma descrição explícita das diferenças em relação à referência. A ausência de evidência não será tratada como confirmação de que o Medal possui ou não possui um mecanismo.
