# Atualização visual do Moment

## Implementação

A interface principal usa a mesma paleta escura e o verde do popup de captura. A revisão inclui tipografia Noto Sans, ícones vetoriais desenhados em Canvas, navegação com indicação de seleção, cartão de estado da captura, cabeçalho da biblioteca com as combinações reais do portal, coleção e área de preview com hierarquia mais clara.

Seletores, sliders, progresso e switch têm estilo próprio. Preferences mantém rolagem; descrições quebram linha e os seletores ficam alinhados. Os botões de captura têm altura mínima de 40 px. O formulário continua usando as preferências e ações existentes.

## Capturas reais e privacidade

As imagens em `artifacts/ui-refresh/` são renderizações de `qml/Main.qml`, capturadas com `QQuickWindow::grabWindow()`. Não são mockups. O teste abre um banco e configurações temporários, importa três imagens de demonstração geradas localmente e usa frames sintéticos. Os indicadores externos de atalhos são fornecidos por uma fixture de teste, sem pedir permissões nem registrar teclas no desktop. Nenhuma gravação pessoal é aberta e o desktop inteiro não é capturado.

Foram renderizadas Library, Capture e Preferences em 1280×820 e 980×660, além do menu do seletor, da parte inferior das Preferences e de uma pesquisa sem resultados. Os PNGs foram inspecionados antes e depois da revisão.

## Verificação reproduzível

```sh
cmake --build build -j2
MOMENT_UI_PREVIEW_DIR="$PWD/artifacts/ui-refresh" ctest --test-dir build --output-on-failure -R '^main-window-visual-test$'
```

O teste verifica carregamento sem erros/avisos QML, todas as páginas e tamanhos, escolha de 60 FPS com mouse/teclado, rolagem até os últimos controles e pesquisa. A renderização offscreen usa CPU; isso não mede desempenho ou captura em gameplay.

## Preferência persistente do usuário

O pedido de sempre renderizar a interface, inspecionar e mostrar o PNG está registrado em `/home/davivbr/.codex/AGENTS.md`. O procedimento deve ser repetido em futuras mudanças de interface, identificando dados de demonstração.


## Ajuste após feedback do usuário

O usuário pediu aparência mais funcional e menos promocional. A revisão seguinte removeu o banner, slogans e chamadas decorativas, reduziu os raios dos painéis e deu mais área ao preview. As capturas atuais ficam em `artifacts/ui-simple/`; usam o mesmo teste e as mesmas mídias sintéticas. O teste visual passou após a alteração.
