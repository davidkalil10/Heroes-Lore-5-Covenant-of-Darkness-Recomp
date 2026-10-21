## Heroes Lore 5 — v1.1.3

### Configurações integradas ao jogo

- Nova entrada **Opções do port** dentro das configurações originais, no título e durante a campanha.
- A engrenagem flutuante foi removida. A nova linha acompanha o tamanho do menu original e aceita toque/clique, teclado e controle.
- **F10 e R3** continuam abrindo o painel diretamente. Voltar fecha o painel e mantém as opções originais disponíveis.
- As opções originais continuam funcionando sem abrir duas janelas ao mesmo tempo.

### Correção do idioma após atualizar

- Corrigida a extração Android que podia abrir a cópia antiga de um recurso e truncá-la durante a atualização.
- A nova versão lê os recursos diretamente do APK, valida tamanho e SHA-256 e só então substitui a cópia extraída.
- O dicionário PT-BR completo também está incorporado ao executável: menus, diálogos e o painel personalizado usam a mesma tradução, independentemente da cópia externa.
- Troca de idioma em tempo real preservada; PT-BR continua sendo o padrão. “Velocidade” foi abreviado para caber antes do valor nas opções originais.

### Atualização

Versão **1.1.3**, **versionCode 5** no Android. Assinatura, nomes dos pacotes e caminhos dos saves foram mantidos. Atualize pelo verificador do aplicativo ou pelos arquivos desta release; não é necessário desinstalar.

| Plataforma | Pacote |
| --- | --- |
| Windows x64 | `heroes_lore_5_windows_x64.zip` |
| Linux / Steam Deck | `heroes_lore_5_linux_x86_64.AppImage` |
| Android | `heroes_lore_5_android_arm64.apk` ou `heroes_lore_5_android_universal.apk` |
| Nintendo Switch | `heroes_lore_5.nro` |

O `.exe` avulso é destinado a instalações Windows que já possuem recursos e dependências. Para instalação nova, use o ZIP completo.

### Verificações

Testes automatizados de CPU/áudio, expansão de cenário, idioma ao vivo, nuvem e empacotamento, além de substituição segura de recursos e navegação nas configurações originais. README e guias de continuidade foram atualizados. A conferência final de atualização e apresentação no Android/Switch depende dos testes nos aparelhos.
