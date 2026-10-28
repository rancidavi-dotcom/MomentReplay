# Avaliação de viabilidade: OpenCut como editor do Moment

**Data da consulta:** 27 de outubro de 2026  
**Escopo:** pesquisa em repositórios e documentação pública; nenhum código OpenCut foi baixado, integrado ou alterado.

**Reverificação nesta sessão:** o README continua indicando que o editor está sendo reescrito e aponta o Classic como versão para uso atual. O README oficial do desktop da rewrite esclarece que o programa é muito inicial e, no momento, apenas abre uma janela. O plano M1 publicado descreve interfaces de engine/API, mas a interface NLE completa fica para marcos posteriores; portanto, a existência desse plano não significa que há agora um editor pronto para incorporar.

## Conclusão executiva

**É tecnicamente possível integrar OpenCut ao Moment, mas não como um componente editor pronto para embutir hoje.** O repositório principal (`OpenCut-app/OpenCut`) está em reescrita e apresenta Editor API, plugins, core Rust multiplataforma e modo headless como objetivos em construção. Seu README recomenda a versão clássica para uso atual.

A versão clássica tem um editor web real e código-fonte aberto sob MIT, mas foi arquivada em 17 de maio de 2026. A documentação pública descreve uma aplicação Next.js, workspaces Bun/Turbo e uma parte Rust/WASM; o desktop GPUI constava como trabalho em andamento. Não foi encontrado um contrato público e estável de integração entre uma aplicação Qt e o estado interno do editor. Logo, uma integração imediata teria de manter um fork/embedded web editor ou rodar o editor como processo separado e definir nossa própria ponte de mídia/projetos.

**Recomendação:** não substituir agora o editor do Moment pelo OpenCut. Primeiro fazer uma avaliação isolada da versão clássica, sem dependências no app principal, para medir importação de arquivo local, edição, render/export, persistência offline e empacotamento Linux. Integrar apenas se esses fluxos funcionarem sem backend remoto e com uma fronteira IPC/arquivo bem definida. Em paralelo, acompanhar a rewrite e reavaliar quando a API do editor estiver publicada.

## Repositórios pesquisados

### OpenCut atual — `OpenCut-app/OpenCut`

- O README afirma que o produto está sendo reescrito desde o início.
- A lista de coisas futuras inclui Editor API, plugins de terceiros, desktop/mobile/browser usando core Rust, MCP e headless mode.
- O README direciona quem precisa usar o produto agora para `opencut-classic`; a versão nova ainda não assumiu o lugar do editor atual.
- O repositório é MIT, segundo seu README e arquivo de licença.
- O projeto fornece uma tarefa de desenvolvimento `desktop:dev`, mas isso por si só não prova que há uma release desktop estável ou uma API de incorporação.

### OpenCut Classic — `OpenCut-app/opencut-classic`

- O proprietário arquivou o repositório em 17 de maio de 2026; está somente leitura.
- MIT, mas a licença exige preservar aviso de copyright e texto da licença em cópias/substanciais.
- A raiz do projeto usa Bun workspaces e scripts Turbo; a documentação descreve Next.js para a aplicação web e Rust/WASM para partes do motor. A documentação de desktop o descreve como GPUI em andamento.
- O repositório não tem releases GitHub publicadas, de acordo com a página de releases consultada.
- Um hook interno expõe `EditorCore.getInstance()` para os próprios componentes React. Isso mostra que existe uma core interna consumida pela UI, mas não equivale a uma API pública estável/semântica suportada por um host Qt.
- A existência de serviços/infra em sua configuração e documentação significa que modo completamente local e sem autenticação/backends precisa ser provado experimentalmente; não deve ser presumido.

## Compatibilidade com Moment

O Moment hoje é um aplicativo C++/Qt Quick com biblioteca local em SQLite e caminhos de mídia locais. OpenCut Classic é uma aplicação web/TypeScript com componentes Rust/WASM. A compatibilidade é possível por estas fronteiras, em ordem de menor acoplamento:

1. **Processo/editor separado:** Moment abre um projeto ou mídia local no OpenCut e troca caminhos/resultados via arquivo de projeto e IPC local. É a opção mais isolada, mas depende de um formato de projeto/importação que a build selecionada realmente suporte.
2. **WebView em janela de editor:** empacotar a aplicação web numa WebView ou servi-la localmente e criar uma ponte estreita para seleção/export de arquivos. Isso preserva uma UI coesa, mas aumenta tamanho, superfície de segurança, complexidade de foco/atalhos e manutenção de um fork.
3. **Portar o motor/core:** consumir apenas a parte Rust/WASM e construir nova UI Qt. A separação e estabilidade do core da versão clássica não estão garantidas; isso pode virar uma integração profunda/fork.
4. **Usar o futuro Editor API:** seria o encaixe arquitetural mais limpo, mas a própria lista de status do repositório atual o apresenta como trabalho futuro, não como contrato disponível hoje.

### Riscos de produto e engenharia

- **Ciclo de manutenção:** o Classic está arquivado; correções Linux, dependências e segurança ficariam sob nossa responsabilidade.
- **Exportação:** os próprios documentos clássicos listam exportação entre áreas em refatoração. É preciso demonstrar render confiável antes de substituir o fluxo de clipe.
- **Privacidade/offline:** a proposta do Moment é edição local; testar sem serviços/conta requer inspeção do código e execução controlada, não inferência pelo slogan de privacidade.
- **Licença:** MIT permite adaptação e distribuição, sujeito aos avisos. Todas as dependências transitivas e assets também precisam de auditoria de licença/proveniência antes de bundling.
- **Identidade visual:** integração precisa usar a identidade do Moment; não reutilizar logos, marca, assets ou apresentação do Medal. Componentes OpenCut podem ser creditados conforme licença.
- **Fronteira de mídia:** evitar copiar todos os clipes; preferir passagem de URI/caminho e projeto de edição não destrutivo. Validar como editor trata arquivos movidos, áudio, múltiplas trilhas, aspect ratio e export.

## Plano de avaliação posterior (sem integração nesta etapa)

1. Fixar commit/tag do Classic e auditar licença, dependências, scripts e backend necessário.
2. Revisar os fluxos do editor e export no código público; definir uma amostra pequena de mídia sintética/local.
3. Executar o build em sandbox/usuário normal, somente após autorização explícita para execução; não usar conta nem serviço do Medal.
4. Medir importação local, timeline, corte, projeto salvo após reinício, export de MP4 com áudio e falhas offline.
5. Só então comparar três opções: subprocesso separado, WebView com fork, ou manter editor Qt próprio.
6. Reavaliar a rewrite quando documentação/API forem marcadas como utilizáveis e houver release desktop testável.

## Fontes primárias

- [Repositório OpenCut atual e status declarado](https://github.com/opencut-app/opencut)
- [README do desktop atual: protótipo GPUI ainda abre apenas uma janela](https://github.com/OpenCut-app/OpenCut/blob/main/apps/desktop/README.md)
- [Plano público de engine/API e marcos subsequentes de UI](https://github.com/OpenCut-app/OpenCut/issues/867)
- [OpenCut Classic arquivado e descrição da estrutura](https://github.com/opencut-app/opencut-classic)
- [Manifesto de workspaces/scripts do Classic](https://github.com/OpenCut-app/opencut-classic/blob/main/package.json)
- [Licença MIT do Classic](https://github.com/OpenCut-app/opencut-classic/blob/main/LICENSE)
- [Página de releases do Classic](https://github.com/OpenCut-app/opencut-classic/releases)
- [Hook interno `useEditor` no Classic](https://github.com/OpenCut-app/opencut-classic/blob/main/apps/web/src/editor/use-editor.ts)

## Limitações

Esta avaliação é documental. Não clonamos, compilamos, executamos nem integramos o OpenCut. A possibilidade real de editar clipes locais sem backend, o formato de projeto exportável, comportamento do render de produção e requisitos atuais de Linux dependem de uma avaliação estática/dinâmica específica da revisão escolhida.
