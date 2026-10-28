# Captura de áudio — primeira fatia

## Escopo

Esta vertical adiciona uma fonte PCM selecionável para a captura de tela ativa do Moment. O mesmo fluxo local alimenta o buffer de replay e, durante uma sessão, a faixa de áudio do MP4. Ela é opcional e permanece desligada por padrão.

## Comportamento implementado

- A tela Preferences lista as entradas de áudio expostas por Qt/PipeWire, permite escolher uma e persiste o ID local do dispositivo.
- A captura só começa depois da captura de tela estar ativa e somente com a opção habilitada.
- O áudio é lido de `QAudioSource` em blocos curtos e armazenado em PCM com timestamps monotônicos.
- O replay mantém blocos em uma janela temporal limitada e recorta os blocos para a janela selecionada ao exportar.
- A exportação do replay usa `QAudioBufferInput` e solicita AAC junto com H.264/MP4 quando há áudio.
- A gravação contínua usa o `QAudioBufferInput` na mesma `QMediaCaptureSession`; uma fila de até 4 MiB limita dados não aceitos pelo encoder.
- Se a pessoa ativar áudio e não houver entrada/formato/capacidade AAC compatível, a interface mostra o problema em vez de registrar silenciosamente um vídeo sem som.
- Ao desativar áudio, os blocos temporários são limpos. Capturas de tela não recebem nenhuma amostra de áudio.

## Metodologia e evidência Medal/REA

A investigação do pacote Windows permanece estática. `ScopePipeline.Build` cria objetos de captura/processamento de áudio, `AudioFrameQueue`, encoders e `AudioPacketTee`; `RecordingSession.RegenerateAudioCapture` atualiza a captura sob sincronização. `ClipOutput` mantém buffers de áudio associados ao pipeline e exporta faixa de áudio junto com vídeo.

O REA/Ghidra 12.1.4 examinou `scope.dll` SHA-256 `a68131597ec9f82a45628271a82dc79e90e1e19b85609591f020c40b38e9c7b9`. A análise do corpo `FUN_1800d4950` em `0x1800d4950` está registrada como `ev_a9e69e4deedfe006d32f986bdfc941e05b3f3da6ae2418fcdb89e7f42a98f1a4`; a página de pseudocódigo consultada nesta continuação é `ev_a998dc915e20a5d879faf5f622e916db053acc9022f3032b153b8eed59af2ee0`.

`FUN_1800d4950` percorre referências a chunks sob lock, compara os intervalos com o intervalo temporal de entrada e constrói uma coleção selecionada. O pseudocódigo registra instanciação com `AudioPacket`, `AudioChunkInfo` e `TimeDataExporter`. Isso reforça a seleção temporal no buffer de áudio, mas **não** revela dispositivo, formato de amostra, clock mestre, alinhamento final, codec usado em runtime, nem política completa de armazenamento. A análise da função é código descompilado gerado pelo Ghidra, não código-fonte original. Consulte [04_RECORDING_ENGINE.md](../../medal_reverse_engineering/04_RECORDING_ENGINE.md) e [EVIDENCE_INDEX.md](../../medal_reverse_engineering/EVIDENCE_INDEX.md) para IDs e limitações canônicos.

O REA não demonstra que o Medal usa Qt, PipeWire ou a arquitetura desta implementação. Essas tecnologias são escolhas independentes para Linux. O caminho `renderer-Audio.js` continua com imports dinâmicos/não resolvidos e não prova o comportamento completo da UI.

## Testes e estado

- `instant-replay-test`: gera vídeo e PCM sintéticos, exporta MP4 e confirma que o Qt consegue decodificar buffers de áudio do arquivo.
- `recording-preferences-test`: confirma persistência da opção e ID de entrada.
- `screen-recorder-test`: confirma gravação de vídeo sem áudio e mantém o default de áudio desligado.
- `ctest --test-dir build --output-on-failure`: 5/5 testes passaram nesta revisão.

O usuário confirmou em 2026-10-27 que o Moment capturou com sucesso o microfone. Essa validação é reportada manualmente; não registramos dispositivo, duração, arquivo de saída ou medida de sincronização. A fixture sintética comprova mux/decode no backend disponível, não captura física de monitor PipeWire. Uma entrada de monitor pode existir entre as fontes expostas, mas não há garantia de captura de áudio de sistema em todos os compositores; não existe captura por aplicativo nem faixas separadas de microfone/sistema.

## Diferenças para o Medal e questões abertas

- Há uma única entrada escolhida, ao passo que a documentação pública do Medal oferece seleção mais ampla de áudio do PC/aplicações e fluxos multifaixa em alguns modos.
- Não há captura per-app, faixas independentes, mixagem, mute por fonte, medidor, supressão de ruído, teste de microfone ou editor multifaixa.
- A origem temporal usa relógio monotônico local e timestamps dos blocos; ainda faltam uma medição de drift sob captura real, teste de troca/desconexão de dispositivo e validação de sobrecarga.
- A integração está baseada em `QAudioSource` e `QAudioBufferInput`, que requer o backend FFmpeg do Qt para esta forma de entrada customizada. O build precisa reportar erro claramente quando MP4/H.264/AAC não estiver disponível.

## Próxima investigação recomendada

Validar a seleção real de microfone e monitor PipeWire no compositor do usuário, medir sincronização A/V em uma sessão de alguns minutos e, em paralelo, aprofundar no REA a regeneração de áudio e `CheckBufferAlignment` do `RecordingSession`. A próxima etapa de produto é separar fontes de áudio em trilhas distintas somente depois de fechar esses contratos de captura e timestamps.
