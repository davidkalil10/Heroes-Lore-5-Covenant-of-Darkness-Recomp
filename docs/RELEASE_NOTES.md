## Heroes Lore 5 — v1.1.2

### Interface revisada

- **Engrenagem menor em tela cheia:** tamanho limitado e posição acima do botão original de menu/voltar, com o mesmo limite usado no clique/toque.
- **Configurações independentes:** a opção original do título abre apenas as opções do jogo. O painel do port abre pela engrenagem, F10 ou R3 e pode ser fechado normalmente.
- **Rótulos PT-BR por contexto:** os títulos mantêm nomes completos; botões da grade e abas usam formas curtas, sem invadir seus vizinhos.
- **Atributos revisados:** ATQ, DEF fís., DEF mág., Vel. atq., Resist. e outros rótulos respeitam as colunas dos valores.
- **Mapa / Comprar / Salvar:** a tradução usa “Comprar”, com ajuste de largura dentro dos botões do HUD nos modos Native e Wide.
- Removida a linha redundante “Opções do jogo” do painel do port, que apenas fechava a janela.

### Atualizar

Use a verificação ao iniciar ou **Configurações → Verificar atualizações**. Esta versão é **1.1.2**, com **versionCode 4** no Android. A assinatura e os nomes dos pacotes foram mantidos. Não é necessário desinstalar o aplicativo.

| Plataforma | Pacote |
| --- | --- |
| Windows x64 | `heroes_lore_5_windows_x64.zip` |
| Linux / Steam Deck | `heroes_lore_5_linux_x86_64.AppImage` |
| Android | `heroes_lore_5_android_arm64.apk` ou `heroes_lore_5_android_universal.apk` |
| Nintendo Switch | `heroes_lore_5.nro` |

O `.exe` avulso serve para instalações Windows que já possuem os recursos e dependências. Para uma instalação nova, extraia o ZIP completo.

### Validação e continuidade

O autor confirmou na versão anterior a atualização automática Android, troca de idioma e restauração de saves com retorno ao título sem fechar o app. A atualização automática do Switch fica para o teste desta release.

Novos testes reproduzem grade, equipamento, habilidades, HUD em Native/Wide e entrada/saída das opções originais. A comparação de pixels verifica que a tradução do HUD fica dentro dos botões. Continuam as verificações de CPU, áudio, cenário expandido, idioma ao vivo, saves e empacotamento.

README e guias de engenharia foram atualizados. Os novos ajustes visuais ainda devem ser conferidos nos aparelhos reais.
