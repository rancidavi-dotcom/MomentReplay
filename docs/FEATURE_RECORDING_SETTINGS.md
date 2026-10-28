# Perfil de gravação local

## Estado

Vertical implementada e testada: a tela Preferences permite escolher 30 ou 60 FPS, cinco níveis de qualidade, janela replay de 5 a 120 segundos e comprimento de clipe de 15 a 120 segundos. As opções são validadas e persistidas em `settings.ini` dentro do diretório de dados da aplicação. A janela e o comprimento de replay afetam exportações seguintes; um clipe só pode conter o tempo que já está no buffer.

Este comportamento é uma decisão original do projeto Linux. A análise do Medal confirmou a presença de módulos renderer de configurações/modos de gravação e da entrega de settings ao encoder, mas ainda não relacionou os controles exatos da interface aos campos e valores internos do encoder. Não declarar equivalência de opções ou defaults.

## Implementação

- `RecordingPreferences` valida e persiste o perfil usando `QSettings` em arquivo INI.
- A janela do replay também é persistida e aplicada em `InstantReplay`; o caminho do arquivo é recebido explicitamente para respeitar `--data-dir` e permitir testes isolados.
- Valores aceitos: FPS 30/60; qualidade Qt Multimedia 0–4 (`Very low` a `Very high`). Valores fora dos limites não alteram estado nem arquivo.
- O comprimento configurado é compartilhado entre o botão **Save replay** e a hotkey global; o arquivo fica limitado pela janela configurada e pelo tempo já acumulado.
- `ScreenRecorder` lê o perfil no início da gravação. Mudanças durante uma sessão afetam apenas a próxima sessão.
- A UI tem uma área Preferences própria com rolagem vertical, para alcançar as opções em janelas menores. Nenhum elemento gráfico ou texto do Medal foi reutilizado.

## Verificação

- `recording-preferences-test`: verifica defaults, persistência em nova instância e rejeição de valores inválidos.
- `screen-recorder-test`: verifica MP4 com frames sintéticos e import para a biblioteca usando o perfil injetado.
- Build CMake concluído; `ctest --test-dir build --output-on-failure`: 5/5 passaram.
- Smoke QML com `QT_QPA_PLATFORM=offscreen`: a aplicação permaneceu aberta por 3 s, sem logs; terminou pelo timeout esperado.
- Após adicionar a rolagem, smoke QML de 5 s também permaneceu sem logs; gesto de rolagem ainda não foi verificado por interação automatizada.

## Limitações

O teste de gravação não prova que 60 FPS ou cada nível de qualidade seja suportado pelo backend real do host. O Qt Multimedia pode escolher encoder por plugin; a UI ainda não consulta capabilities nem oferece resolução, bitrate, seleção de encoder, diretório de saída, mixagem ou fontes/faixas separadas. Uma única entrada de áudio opcional agora existe em Preferences (ver `FEATURE_AUDIO.md`); o usuário confirmou que o microfone foi capturado. Sincronização A/V e equivalência com as telas/valores do Medal continuam sem validação.

## Próximo passo

Investigar o schema managed de `EncoderSettings` e os caminhos renderer de Settings/RecorderModes com REA; depois enriquecer a tela somente com opções que tenham objetivo de usuário demonstrável e suporte Linux mensurável. Separadamente, validar uma captura real autorizada no portal Wayland.
