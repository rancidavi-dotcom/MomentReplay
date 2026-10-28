# Primeira vertical do editor do Moment

Estado: **Especificado para implementação incremental**. O editor ainda não está implementado. Evidência da referência: [investigação do editor Medal](../../medal_reverse_engineering/10_VIDEO_EDITOR.md). Esta especificação usa comportamentos e requisitos; nenhum carrier extraído do Medal entra no código ou build do Moment.

## Requisitos sustentados pela referência

- Projeto separado do arquivo de mídia, com cortes representados em um modelo editável.
- Timeline com FPS e limites de corte coerentes com frames.
- Salvar o projeto é diferente de renderizar mídia.
- Exportação produz um novo arquivo, com estados de andamento, conclusão, erro e cancelamento.
- Integração do arquivo exportado à biblioteca local.

As estruturas e callbacks internos do Medal foram observados estaticamente. O formato completo e o comportamento runtime da referência continuam parcialmente desconhecidos.

## Fluxo proposto para Linux — arquitetura própria

1. Selecionar um **vídeo** na Library e abrir a ação Edit.
2. Abrir uma área de edição com player, linha temporal e dois marcadores de início/fim; exibir duração original e selecionada. Estado vazio/erro se a mídia estiver indisponível ou não decodificar.
3. Permitir ajustar os marcadores por mouse e teclado, com precisão visível. Não sobrescrever o arquivo de entrada.
4. Salvar um projeto do Moment com versão de schema, ID próprio, referência/fingerprint da mídia, intervalo e timestamps. Usar escrita atômica e reabrir mantendo o intervalo.
5. Exportar um novo arquivo a partir do intervalo confirmado, com progresso e cancelamento. A estratégia de corte em keyframe versus recodificação precisa ser escolhida explicitamente para preservar precisão.
6. Importar a saída finalizada à Library e mostrar o popup próprio de sucesso. Falha ou cancelamento não cria um item concluído nem deixa um arquivo parcial parecendo válido.

O primeiro modelo pode ter uma única faixa e um vídeo. Deve permitir evolução para split, múltiplos segmentos, undo/redo e áudio sem recomeçar o armazenamento. O marco inicial não reduz o escopo geral do produto.

## Contratos de implementação propostos

- `EditorProject`: modelo original do Moment, separado de `ClipRepository`; trabalha com intervalo em unidades inteiras de tempo e frame rate informado pela mídia.
- Player Qt já existente: preview de vídeo e posição; conexão ao modelo de edição sem alterar o pipeline de replay.
- Persistência: arquivo/registro de projeto versionado, referências à origem e escrita atômica; sem salvar blobs de vídeo dentro do JSON.
- `EditorExportJob`: interface assíncrona de progresso/cancelamento/resultado. Avaliar o backend de mídia disponível para precisão A/V, comportamento em arquivo longo e manutenção antes de fixar a implementação.
- `ClipLibraryModel`: recebe apenas um arquivo final válido. O vínculo com o projeto permite voltar à edição.
- Popup atual: confirma exportação somente depois de o arquivo estar finalizado.

## Critérios de aceite e testes necessários

- O hash do vídeo original permanece igual ao salvar projeto, reabrir e exportar.
- O intervalo escolhido reaparece após reiniciar a aplicação.
- Entrada/saída inválidas, arquivo ausente e duração desconhecida têm estados claros; nenhum export inicia com intervalo inválido.
- A saída decodifica e sua duração corresponde ao intervalo dentro da precisão declarada; testar cortes fora de keyframes.
- Vídeo com áudio continua com timestamps válidos e sync A/V verificado com mídia sintética.
- Cancelar encerra o trabalho e remove/identifica o parcial; não importa um falso sucesso.
- Não bloquear o event loop da UI durante exportação.
- Renderizar a UI real com mídia sintética, inspecionar PNG e apresentar no chat, conforme preferência persistente do usuário.

## Ordem seguinte

1. Trim + projeto + export + Library, como fluxo completo.
2. Split e undo/redo, com os mesmos projetos.
3. Múltiplos segmentos e faixas de áudio.
4. Transformações visuais e demais recursos, cada um acompanhado por investigação REA e critérios de equivalência.

O uso de OpenCut continua sendo uma decisão de integração em avaliação; essa pesquisa do Medal não demonstra que OpenCut seja uma dependência pronta para o app Qt.
