# MomentReplay — aplicativo nativo de captura para Linux

## Objetivo

Construir uma aplicação desktop nativa para Linux com ampla equivalência funcional à experiência de um gravador de gameplay moderno, usando código original e serviços próprios. Este projeto não contém binários, código ou recursos proprietários do Medal. A pesquisa REA usada para orientar os requisitos permanece em um diretório local separado e não é necessária para compilar o Moment.

## Estado atual

- **Investigação:** os requisitos foram orientados por análise estática local e documentação pública; detalhes de proveniência ficam fora deste repositório para manter o código distribuído independente.
- **Planejamento:** proposta de arquitetura e marcos em [docs/LINUX_ARCHITECTURE_AND_ROADMAP.md](docs/LINUX_ARCHITECTURE_AND_ROADMAP.md), com o estado por funcionalidade em [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md).
- **Código Linux:** App Moment com áreas próprias de Library, Capture e Preferences roláveis. Fechar a janela oculta o app na bandeja do sistema quando disponível; o ícone permite reabrir ou encerrar. A Library usa SQLite, importação por seletor, pesquisa, favoritos, remoção segura do índice e player Qt Multimedia; aceita imagens e vídeos. Capture usa XDG ScreenCast + PipeWire/GStreamer, mantém vídeo JPEG e áudio PCM opcional no replay, exporta MP4/H.264 com áudio AAC quando habilitado e salva screenshots em PNG. Preferences persiste FPS, qualidade, duração do replay e do clipe, além da fonte de áudio; o áudio fica desligado por padrão. Hotkeys globais usam o portal Wayland (F8 preferido para replay; F9 para screenshot), reativados por `BindShortcuts` em cada sessão. Um popup próprio confirma o salvamento no canto da tela. A detecção local reconhece candidatos de bibliotecas comuns e oferece cadastro manual por executável; perfis podem definir FPS, qualidade, buffer e clipe, além de iniciar/parar gravação contínua automaticamente. O usuário confirmou a captura de tela e do microfone; editor, eventos de kills, faixas separadas e recursos de nuvem seguem pendentes.
- **Testes Linux:** build aprovado. O teste visual usa QML real com mídia de demonstração isolada; testes de perfis usam uma árvore `/proc` sintética e verificam persistência/overrides. Ainda não há validação manual de gravação automática com um jogo real, acionamento de F8/F9 durante gameplay, áudio separado ou equivalência com o Medal.
- **Visual:** Library, Capture e Preferences receberam uma paleta e controles próprios, alinhados ao popup. As capturas reais com mídias de demonstração e os testes de interface estão em [UI_VISUAL_REFRESH.md](docs/UI_VISUAL_REFRESH.md).
- **Licença:** MIT para o código deste repositório. As licenças e avisos das dependências de distribuição continuam sendo auditados antes de releases empacotados.
- **Ghidra:** cópia local de Ghidra 12.1.4 + JDK 21 em diretório do usuário e configurada para a próxima inicialização do servidor REA. REA CLI concluiu overview de `scope.dll` e dossier Ghidra de `scope_io_buffer_export_from_timestamp`; o MCP conectado ainda está com o ambiente anterior.

## Princípios de engenharia

1. Investigar cada comportamento no Medal com REA e guardar Evidence/hash antes de traduzir requisitos.
2. Implementar por fluxos verticais, sem tratar nomes de chunks, imports ou strings como comportamento provado.
3. Código, imagens, marca, ícones e recursos próprios; nenhuma dependência de binário Medal.
4. Biblioteca local funciona offline; serviços de nuvem só via backend e protocolos próprios documentados.
5. Wayland é o alvo inicial; respeitar consentimento do portal. X11 terá backend/adaptação e testes próprios.
6. Cada feature mantém o estado e as diferenças na matriz; paridade só após teste comparativo registrado.

## Build e testes locais

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Para iniciar isolando o banco em uma pasta descartável: `QT_QPA_PLATFORM=offscreen ./build/medal-linux --data-dir /tmp/medal-linux-smoke`. O argumento `--data-dir` permite testar sem tocar no perfil persistente do usuário. Detalhes da bandeja estão em [FEATURE_SYSTEM_TRAY.md](docs/FEATURE_SYSTEM_TRAY.md); screenshot em [FEATURE_SCREENSHOTS.md](docs/FEATURE_SCREENSHOTS.md), replay em [FEATURE_INSTANT_REPLAY.md](docs/FEATURE_INSTANT_REPLAY.md), áudio em [FEATURE_AUDIO.md](docs/FEATURE_AUDIO.md) e perfis em [FEATURE_GAME_PROFILES.md](docs/FEATURE_GAME_PROFILES.md).

## Arch Linux / AUR

O repositório inclui um `PKGBUILD` para Arch Linux. Depois que o pacote for publicado no AUR, a instalação será:

```sh
yay -S momentreplay
```

Para testar localmente antes da publicação, execute `makepkg -Ccf` no diretório do projeto. O pacote compila a revisão do branch `main`, instala o binário em `/usr/bin/medal-linux` e registra o lançador `io.moment.Moment`.

## Licenciamento e proveniência

Nenhum conteúdo proprietário foi copiado. Antes de selecionar dependências, registrar versão, licença de código/plugins/codec e distribuição. Qt e FFmpeg têm componentes/opções com obrigações diferentes; ver a seção de licenças no plano. A lista de SBOM e avisos deve acompanhar cada release.
