## Heroes Lore 5 — v1.1.1

### Novidades

- **Português do Brasil como padrão**, com inglês disponível em **Configurações → Idioma**. Troque durante a partida, inclusive com diálogos e menus abertos, sem reiniciar ou sair do mapa.
- **Versão do port na tela de título**, junto da região do “Tap to Continue”, e no painel de configurações.
- **Engrenagem no canto superior direito** para abrir as configurações. O botão separado de aspect ratio foi removido; a opção continua no painel, em F9 e no atalho Select+Start (−/+).
- **Menu maior no Android**, com tamanho de painel, textos e linhas calculado pela tela disponível.
- **Fonte do menu no Switch corrigida**: caminho RomFS normalizado, criação de textura ajustada e fonte de emergência para falhas de carregamento/renderização.
- **Restaurar saves volta ao título sem fechar o aplicativo**. O estado antigo é liberado antes da troca dos arquivos; os slots são recarregados a partir do backup restaurado.
- README no estilo dos outros ports, documentação de continuidade atualizada e ferramentas Python para versão e tradução.

### Atualização e downloads

Abra **Settings → Check for updates** na v1.1.0, ou aguarde a consulta ao iniciar. A nova versão é **1.1.1**; Android usa **versionCode 3**. A instalação preserva saves e preferências. Não desinstale o app Android antes de atualizar.

| Plataforma | Arquivo |
| --- | --- |
| Windows x64 | `heroes_lore_5_windows_x64.zip` — extraia o pacote completo |
| Linux / Steam Deck | `heroes_lore_5_linux_x86_64.AppImage` |
| Android | `heroes_lore_5_android_universal.apk` ou `heroes_lore_5_android_arm64.apk` |
| Nintendo Switch | `heroes_lore_5.nro` |

O `.exe` avulso serve para instalações Windows que já possuem os recursos e dependências. Uma instalação nova deve usar o ZIP.

### Cloud save

Salve dentro do jogo antes de enviar o backup. Em outro aparelho, conecte a mesma conta Google e restaure pela opção de saves na nuvem. O jogo retorna ao título; selecione o slot para continuar. A cópia local anterior continua protegida em `save-backups`.

### Validação

Testes locais cobrem CPU, áudio, expansão de cenário, troca EN → PT-BR → EN em diálogo ativo, UTF-8/comandos de formatação, retorno ao título, restauração simulada, leiaute móvel e empacotamento sem dados pessoais. As builds das quatro plataformas são produzidas pelo Actions.

A tradução teve preparação automática e revisão de termos/formatação; a campanha completa ainda pode receber ajustes de texto. Os novos fluxos de atualização, menu e restauração devem ser conferidos no Android e no Switch reais.
