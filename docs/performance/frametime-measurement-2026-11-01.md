# Medição de desempenho durante o salvamento de replay

Data: 2026-11-01
Sistema: CachyOS, Wayland, NVIDIA GeForce GTX 1660 Ti
Jogo observado: Rocket League via Proton
Aplicativo observado: Moment, build local instalada em `~/.local/bin/medal-linux`

## Resultado principal

O teste mediu CPU, espera de escalonamento dos processos, estado da GPU e o arquivo produzido ao redor de um único F8. **Não foi possível obter frametime real por quadro nesta sessão.** Rocket League estava rodando sem MangoHud e o contador não pode ser anexado retroativamente de forma segura. Portanto, os números abaixo não provam ausência de engasgos visíveis.

## Procedimento

- O Moment permaneceu oculto; nenhuma janela foi aberta.
- Antes do teste, foi confirmado que Rocket League tinha o foco e que o Moment estava em prioridade inferior (`nice 10`; jogo `nice -5`).
- Foram amostrados os contadores de CPU e `schedstat` em `/proc` por thread: 7,65 s de baseline, um envio de F8, depois 29,79 s de observação.
- `nvidia-smi` foi consultado. A coleta `dmon` iniciada em segundo plano terminou antes do trecho F8, então seus poucos pontos não são usados como medição da GPU durante o salvamento. Uma coleta curta posterior ao teste foi registrada separadamente.
- O clipe recém-criado foi inspecionado com `ffprobe`; o vídeo não foi aberto nem reproduzido.
- Não foram lidos argumentos ou variáveis de ambiente do jogo.

## Medidas de CPU e escalonamento

CPU é apresentada como percentual de um núcleo lógico: 100% equivale a um núcleo ocupado durante todo o intervalo. A espera de escalonamento é a soma do tempo de espera dos threads do processo, dividida pelo tempo de parede; é uma métrica agregada, não uma medida de latência de frame.

| Janela | Rocket League CPU | Jogo: espera agregada na fila | Moment CPU | Moment: espera agregada na fila |
|---|---:|---:|---:|---:|
| Baseline, 7,65 s | 119,9% | 17,3 ms por segundo | 93,7% | 11,4 ms por segundo |
| Primeiros 3,07 s após F8 | 102,0% | 18,3 ms por segundo | 157,7% | 23,1 ms por segundo |
| Primeiros 6,14 s após F8 | 113,0% | 20,8 ms por segundo | 212,9% | 39,3 ms por segundo |
| Primeiros 10,26 s após F8 | 116,3% | 48,9 ms por segundo | 323,3% | 94,9 ms por segundo |
| Janela completa após F8, 29,79 s | 121,6% | 48,7 ms por segundo | 411,0% | 114,6 ms por segundo |

O contador de CPU do Moment cresce durante a janela, chegando a uma média de **4,11 núcleos lógicos** no intervalo de 29,79 s. Isso é carga significativa durante a operação. O jogo manteve uso médio próximo ao baseline, mas a espera agregada de seus threads subiu de 17,3 para 48,7 ms por segundo. Como os contadores são agregados entre até 94 threads do jogo, esse aumento **não** revela se o thread de renderização esperou o bastante para causar um frame perdido.

O jogo tinha prioridade de escalonamento mais alta que o Moment. Isso ajuda a protegê-lo sob contenção de CPU, mas não elimina contenção de CPU/GPU nem garante frametime estável.

## GPU

Uma amostra imediatamente antes do F8 indicava 40% de utilização do GPU, 13% de memória, 51 °C e 48,23 W. A coleta durante o trecho F8 não foi preservada, portanto esses valores não podem ser comparados diretamente com o período de codificação.

Na coleta curta posterior ao teste: GPU 34–37%, memória 10–11%, 49 °C e 37–39 W. Não há evidência de pico de GPU no período posterior; isso não exclui um pico transitório durante o F8.

## Arquivo produzido no teste

`~/Vídeos/Moment/Replay-20261101-155634-bfb5721d.mp4`

- Duração: 15,0665 s
- Tamanho: 147.372.992 bytes (aprox. 140,5 MiB)
- Vídeo: H.264, 1917×583, `yuv444p`, aproximadamente 30 fps
- Bitrate total médio: 78,25 Mbit/s
- Áudio: AAC, 48 kHz, mono

O tamanho e o formato são dados observados do arquivo. Não se concluiu que o formato `yuv444p` seja a causa do engasgo, mas a combinação de arquivo grande e média de 4,11 núcleos durante a janela justifica investigar a exportação/codificação antes de repetir o teste.

## Interpretação e limite da conclusão

**Confirmado:** um único F8 salvou um MP4 válido de 15,07 s; o Moment consumiu em média 4,11 núcleos durante a janela de 29,79 s; a prioridade do jogo era maior; CPU média do jogo ficou próxima do baseline.

**Não confirmado:** que o jogo não teve hitch. Média de CPU e espera agregada não substituem frametime por quadro; não há percentis de 1% lows, picos em milissegundos ou série temporal de frames nesta execução.

## Próxima medição necessária

Para medir frametime de verdade, iniciar uma próxima sessão do jogo com MangoHud habilitado e registrar a telemetria de frametime enquanto o Moment salva um replay. Isso requer reiniciar o jogo, então não foi feito durante esta sessão. A comparação deve guardar frametime de baseline e do F8, além de CPU por thread, GPU/encoder e duração até o arquivo estar finalizado.
