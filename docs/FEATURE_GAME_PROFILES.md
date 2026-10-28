# Detecção local de jogos e perfis

## Estado

Implementação inicial no Moment: o serviço local examina processos de `/proc` a cada 1,5 segundo, reconhece candidatos em caminhos comuns de jogos (Steam, Lutris, Heroic, GOG e diretórios `games`) e permite cadastrar um nome de processo manualmente. Perfis persistem em `settings.ini` e guardam FPS, qualidade, janela do replay, duração do clipe e a opção de iniciar gravação contínua automaticamente.

## Comportamento

- O pareamento usa o nome exato do executável do processo, ignorando maiúsculas/minúsculas. Para processos Wine/Proton, usa o nome `.exe` observado em `/proc/<pid>/comm` quando disponível.
- O perfil ativo altera apenas os valores efetivos da sessão. Os valores globais permanecem intactos e voltam quando nenhum jogo configurado está presente.
- A opção **Auto record** inicia uma gravação contínua quando a captura de tela já está ativa. Ao processo correspondente desaparecer, o Moment finaliza e salva a sessão. Ela fica desligada por padrão em cada perfil.
- Candidatos descobertos pelo caminho aparecem na lista. Executáveis instalados em locais diferentes podem ser adicionados manualmente.
- O scanner não lê argumentos de linha de comando, arquivos de jogo, memória de outros processos, rede ou dados de conta. Ele só lê nomes e links `/proc` acessíveis ao usuário atual.

## Limites conhecidos

Esta é detecção de processo, não identificação garantida de janela em foco. Quando vários jogos configurados estão ativos, o primeiro nome em ordem determinística é usado. Launchers que ocultam ou substituem o processo do jogo, sandboxing que oculta `/proc`, anti-cheat e binários renomeados podem exigir cadastro manual ou não ser detectados. A captura continua sujeita à sessão do XDG ScreenCast e ao compositor; o portal ainda pode capturar o monitor selecionado, não exclusivamente a janela do jogo.

Captura automática de kills/vitórias não está incluída: ela exige uma fonte de eventos específica por jogo, plugin autorizado ou integração documentada. Nenhum mecanismo desse tipo foi inferido aqui.

## Validação

O teste `game-monitor-test` usa uma árvore `/proc` sintética dentro de uma pasta temporária para verificar descoberta, ativação, persistência, opções de perfil e desaparecimento do processo. O teste de preferências confirma que o override do jogo não grava por cima do perfil global. A janela visual usa mídia e nomes de jogo sintéticos marcados como demonstração.
