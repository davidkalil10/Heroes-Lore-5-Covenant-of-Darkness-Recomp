## Heroes Lore 5 — v1.1.4

- **Verificação ao iniciar visível:** mostra a busca e abre o aviso quando existe uma versão nova. Faz novas tentativas após falha de conexão e respeita a preferência de verificação automática.
- **Download com progresso:** barra, percentual e MB recebidos no Switch e nas demais plataformas.
- **Instalador do Switch corrigido:** substituída a cópia que retornava “Function not implemented” por leitura/escrita em blocos. O NRO novo é conferido antes da troca, com backup e recuperação da versão anterior em caso de falha.
- O Switch prepara o relançamento do NRO instalado ao fechar o aplicativo.
- Textos de versão instalada/disponível e download revisados em PT-BR. README e guias de continuidade atualizados.

### Para quem está no Switch com o instalador antigo

**Substitua o NRO manualmente uma vez para receber esta correção**, usando o mesmo caminho no cartão SD. Não apague a pasta de dados/saves. O aplicativo antigo não consegue corrigir sua própria rotina de cópia apenas baixando esta release; as próximas atualizações usarão o instalador corrigido.

### Pacotes e versão

Versão **1.1.4**, **versionCode 6** no Android; assinatura, nomes dos pacotes e caminhos dos saves preservados.

| Plataforma | Pacote |
| --- | --- |
| Windows x64 | `heroes_lore_5_windows_x64.zip` |
| Linux / Steam Deck | `heroes_lore_5_linux_x86_64.AppImage` |
| Android | `heroes_lore_5_android_arm64.apk` ou `heroes_lore_5_android_universal.apk` |
| Nintendo Switch | `heroes_lore_5.nro` |

Para instalação Windows nova, use o ZIP completo; o EXE avulso requer os recursos e dependências já instalados.

### Validação

Testes de checagem automática/repetição/fechamento, download real com progresso intermediário em servidor local, integridade e recuperação da troca do NRO. Mantidas as verificações de CPU, áudio, tela, tradução e nuvem. A confirmação final de instalação e relançamento no Switch depende do teste no aparelho.
