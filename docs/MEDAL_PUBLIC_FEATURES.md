# Recursos públicos do Medal desktop

Pesquisa em páginas oficiais do Medal feita em 27/10/2026. Ela documenta a experiência descrita pela empresa, principalmente para a versão Windows; não prova que cada recurso está presente ou ativo no binário de `/home/davivbr/MEdal/`. A matriz de equivalência deve registrar separadamente os achados de REA e o estado do Moment.

## Captura e gravação

- **Clip mode:** hotkeys para salvar intervalos anteriores; páginas de suporte documentam durações selecionáveis e customizadas, com o atalho F8 como default de documentação. REA local encontrou a configuração managed default F8, porém não determinou override ativo.
- **Long recording / sessões:** início e parada por atalho ou gravação de sessão de jogo; hotkeys de clipe podem servir como bookmarks em uma sessão.
- **Capturas de tela:** atalho dedicado e organização na Media Library.
- **Desktop capture:** selecionar monitor e gravar desktop; a documentação descreve também o retorno automático ao game capture.
- **Detecção de jogos:** detecção automática, entrada manual de jogos sem suporte, presets e troca automática de configurações por jogo.
- **Auto clipping:** clipes de eventos de jogo em títulos suportados, com comprimento configurável. A página pública não especifica a implementação interna do detector.
- **Qualidade:** presets e configurações de resolução, FPS, bitrate, encoder/CPU/GPU e codec. Valores e suporte podem variar pela versão e hardware.

## Áudio e overlays

- Modos All PC Audio ou Specific Apps, dispositivos de saída, escolha de microfone, volumes e teste de microfone.
- Separação de fontes em múltiplas tracks com ajuste na edição; push-to-talk, supressão de ruído e entrada mono aparecem nas páginas oficiais.
- Sobreposições de webcam/câmera e overlays de teclado/mouse são descritos para gravações.

## Biblioteca, edição e compartilhamento

- Media Library agrupa vídeos e screenshots; suporte público descreve filtros por jogo, eventos/favoritos, history, liked e categorias sociais.
- O Quick Trimmer corta e remove áudio; o editor de sessão extrai clipes de uma gravação longa; o Advanced Editor permite trim, split, duplicação e montagem de vários clipes em timeline.
- Editor documentado inclui configuração de exportação; outros materiais públicos mencionam ferramentas de música, texto e assets, cuja disponibilidade pode variar por versão.
- Compartilhamento tem links temporários, publicação em perfil, cloud sync, controles de visibilidade/comentário e destinos como Discord/TikTok. Recursos em nuvem dependem da conta/plano e dos serviços do Medal.
- A experiência social inclui perfis, follows, comentários, marcação de usuários e reações.

## Estado conhecido do Moment e ordem sugerida

1. **Já implementado, ainda sem equivalência Medal validada:** captura contínua de tela consentida, buffer local de vídeo, replay local, gravação de sessão básica, Library local, playback e screenshot PNG do último frame recebido.
2. **Primeira lacuna funcional de alto valor:** áudio do sistema e microfone com fontes/tracks distintas e sincronizadas; PipeWire deve ser investigado e testado no compositor real.
3. **Depois:** duração/combinações configuráveis de hotkeys, bookmarks e seleção de monitor/alvo; perfis por jogo requerem detector próprio com allowlist transparente.
4. **Editor:** primeiro trim não destrutivo e seleção de trechos de uma sessão; depois split/montage/timeline e export controlado.
5. **Backend próprio opcional:** upload resumível, links compartilháveis, identidade e feed requerem serviços próprios, privacidade, moderação e política de armazenamento. Não será usado nenhum endpoint autenticado do Medal.

Esta sequência é uma proposta de engenharia do Moment, não evidência da ordem interna de implementação do Medal. A arquitetura separada de áudio deve vir antes do editor multitrack para evitar um formato local incompatível.

## Fontes oficiais consultadas

- [Getting to Know Medal's Settings](https://support.medal.tv/support/solutions/articles/48001271487/)
- [Getting started with Medal on Windows](https://support.medal.tv/support/solutions/articles/48000959661/)
- [How to choose your recording method](https://support.medal.tv/support/solutions/articles/48001157616)
- [Recording clips overview](https://support.medal.tv/support/solutions/articles/48001157618)
- [Full session recording](https://support.medal.tv/support/solutions/articles/48001180234-how-to-enable-full-length-recording-features)
- [Multi-track recording](https://support.medal.tv/support/solutions/articles/48001220541-multi-track-recording)
- [Advanced editor: trim, split and duplicate](https://support.medal.tv/support/solutions/articles/48001211535/)
- [Montage editor](https://support.medal.tv/support/solutions/articles/48001185258-how-to-create-a-montage)
- [Uploading clips on desktop](https://support.medal.tv/support/solutions/articles/48001170449-how-to-upload-your-medal-clips-on-desktop-app)
- [Sharing clips](https://support.medal.tv/support/solutions/articles/48001169792-how-to-share-your-clips)
- [Commenting on clips](https://support.medal.tv/support/solutions/articles/48001169240/)
