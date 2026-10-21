## Heroes Lore 5 — v1.1.6

Otimizações para investigar e reduzir a lentidão geral relatada no Nintendo Switch, inclusive as pausas ao abrir o menu.

- Desfoque do menu RGB565 executado nativamente, preservando os pixels e o estado original ao final de cada linha.
- Sete novas regiões ARM compiladas: sprites espelhados, transparências, paletas, recorte e efeitos do mapa. Agora são 14 regiões.
- Menos gravações recorrentes no cartão SD durante o jogo; erros e informações de inicialização continuam disponíveis.
- Build Switch com O3, seguindo a otimização já usada no PC/Android.
- Log de desempenho no Switch a cada dez segundos, separando entrada, execução do jogo e apresentação/espera.
- Versão **1.1.6**, Android **versionCode 8**; assinatura, saves e login preservados.

### Validação e limites

As verificações no PC comparam 28 estados ARM, quadros completos, áudio, localização, proporções de tela, serviços e menus. O desfoque também é comparado com o original em Native/Wide/Auto e nos dois idiomas, incluindo memória completa e registradores.

Nos testes locais no PC, a abertura do menu ficou cerca de oito a nove vezes mais rápida. No roteiro do mapa em widescreen, a mediana do tempo médio de processamento caiu de 2,35 para 2,05 ms (cerca de 13%); no modo nativo não houve ganho consistente. Esses números não representam FPS medidos no Switch nem garantem a mesma melhora no aparelho.

### Teste no Switch

1. Atualize pelo próprio jogo e confirme **1.1.6** no título.
2. Compare abertura do menu, movimentação e ataques no mesmo mapa e modo de tela.
3. Se ainda houver lentidão, o arquivo `sdmc:/switch/heroes_lore_5/boot.log` contém linhas `[switch-perf]` para orientar a próxima investigação. `present_wait_ms` inclui espera de sincronização/limitação de quadros; `fps` mede o laço do aplicativo.

### Pacotes

| Plataforma | Pacote |
| --- | --- |
| Windows x64 | `heroes_lore_5_windows_x64.zip` |
| Linux / Steam Deck | `heroes_lore_5_linux_x86_64.AppImage` |
| Android | `heroes_lore_5_android_arm64.apk` ou `heroes_lore_5_android_universal.apk` |
| Nintendo Switch | `heroes_lore_5.nro` |

Para instalação Windows nova, use o ZIP completo. O EXE avulso requer os recursos e dependências já instalados. Versões antigas do Switch com o instalador anterior à 1.1.4 precisam de uma substituição manual do NRO para receber aquele conserto; preserve a pasta de dados.
