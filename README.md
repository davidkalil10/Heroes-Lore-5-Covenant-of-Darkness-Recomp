<div align="center">

<img src="switch/icon.jpg" alt="Heroes Lore 5" width="144" />

# ⚔️ Heroes Lore 5: Covenant of Darkness

### Recompilação nativa · C++17 + SDL2

**Windows · Linux / Steam Deck · Android · Nintendo Switch**

</div>

[![Build multiplataforma](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/actions/workflows/build.yml/badge.svg)](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/actions/workflows/build.yml)
[![Releases](https://img.shields.io/github/v/release/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp)](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/releases)

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](#-compilar-e-validar)
[![Idiomas](https://img.shields.io/badge/Idiomas-PT--BR%20%7C%20English-green)](#-idioma-em-tempo-real)
[![Plataformas](https://img.shields.io/badge/Plataformas-Windows%20%7C%20Linux%20%7C%20Android%20%7C%20Switch-8a62d9)](#-downloads-e-plataformas)

Port de **Heroes Lore 5: Covenant of Darkness** (*EA Mobile Korea*), com
**português do Brasil e inglês**, áudio, controles modernos, saves na nuvem
e expansão real do campo de visão.
O núcleo ARM32 original roda em uma camada C++17 compartilhada pelas plataformas.

## 📥 Downloads e plataformas

Os pacotes prontos para jogar ficam em **[Releases](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/releases/latest)**.

| Plataforma | Pacote |
| --- | --- |
| Windows x64 | ZIP portátil com executáveis e dependências |
| Linux x86_64 / Steam Deck | AppImage |
| Android | APK universal ou ARM64 |
| Nintendo Switch | Homebrew `.nro` com recursos embutidos |

## ✨ Recursos e novidades da v1.1.3

- **Interface PT-BR revisada:** rótulos próprios para botões, abas e colunas de atributos, mantendo os nomes completos nos títulos.
- **Comprar / Salvar no HUD:** a fonte respeita a largura dos botões em Native e Wide, sem invadir os vizinhos.
- **Configurações integradas:** nova linha **Opções do port** dentro das configurações originais do jogo; a engrenagem flutuante foi removida.
- **PT-BR após atualizar:** extração Android lê diretamente do APK e valida os arquivos antes de substituir as cópias antigas. O dicionário completo também fica incorporado ao executável.

### Recursos disponíveis

- 🌎 **PT-BR como idioma padrão:** textos de menus, diálogos, missões, itens e habilidades, com acentos. Inglês continua disponível.
- 🔀 **Troca de idioma durante o jogo:** altere em Configurações; o diálogo e o menu abertos são atualizados sem reiniciar ou sair do mapa.
- 🖥️ **Auto / Wide / Native:** expansão real do cenário, preservando as proporções dos sprites. Auto acompanha janela e orientação.
- 🎮 **Teclado, gamepad e toque:** controles Nintendo seguem os rótulos A/B e X/Y; Select/− troca a fileira de habilidades.
- 🎵 **Áudio e execução otimizados:** mixer Ogg/MP3, regiões ARM recompiladas, índice de arquivos e relógio corrigido.
- ☁️ **Google Drive entre plataformas:** envie o progresso salvo em um aparelho e restaure em outro. Restaurar agora **volta ao título sem fechar o app**.
- 🔄 **Atualizações pelo GitHub:** consulta ao abrir ou pelo menu, download validado e instalação com confirmação.
- ⚙️ **Configurações:** opções originais → **Opções do port**, ou atalho F10/R3. Proporção de tela, idioma, atualizações e nuvem ficam nesse painel.
- 📱 **Painel maior no Android** e carregamento de fonte corrigido no Switch, com fonte de emergência para falhas de carregamento/renderização.
- 🏷️ **Versão instalada no título** e no painel de configurações; metadados atualizados em todas as plataformas.

O autor confirmou desempenho, áudio, modos de tela, backup/restauração entre
aparelhos, troca de idioma e restauração sem fechar o jogo. A atualização
automática no Android também foi confirmada. A atualização do Switch será
conferida nesta release. As correções visuais e de navegação da v1.1.3 têm capturas e testes
automatizados no PC/CI; a conferência final nos dispositivos continua necessária. A tradução foi preparada com assistência automática e revisão de
termos e formatação; sugestões de texto/contexto são bem-vindas durante a campanha.

---

## 🚀 Como jogar

Baixe o pacote da sua plataforma em [Releases](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/releases).
Os [artefatos do Actions](https://github.com/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/actions)
também permitem testar mudanças publicadas na `main` antes de uma release.

1. **Windows:** extraia o ZIP completo e abra `hl5.exe` ou `heroes_lore_5.exe`. Na árvore local, use `dist/hl5.exe`.
2. **Linux:** dê permissão de execução ao AppImage e abra o arquivo. Saves ficam em `~/.local/share/heroes_lore_5` ou no diretório definido por `XDG_DATA_HOME`.
3. **Android:** instale o APK adequado. Atualizações devem manter a mesma assinatura; não desinstale o app para atualizar, pois isso pode apagar os dados locais.
4. **Switch:** coloque o NRO em `/switch/heroes_lore_5/` no SD e abra pelo Homebrew Menu. Os saves ficam separados do NRO.

## ⚙️ Configurações

Abra as configurações originais e escolha **Opções do port**, no final do painel.
Durante o jogo: **menu → Sistema → Configurações → Opções do port**.
Na tela de título: **Configurações → Opções do port**.
Também é possível abrir diretamente pela tecla **F10** ou pelo analógico direito (**R3**).
A nova linha aceita toque/clique; no teclado ou controle, subir a partir da primeira
opção ou descer após a última seleciona a nova entrada. Voltar fecha o painel
personalizado e mantém a tela de opções original disponível.
Use setas/D-pad/analógico para navegar, confirmar para escolher e voltar para fechar.
Mouse e toque também funcionam. O jogo fica pausado enquanto o painel está aberto.

## 🌎 Idioma em tempo real

Em **Configurações → Idioma**, alterne entre **Português (Brasil)** e **English**.
A escolha vale imediatamente para os textos exibidos e é salva neste aparelho.
Não é necessário fechar o jogo nem trocar de mapa; saves e progresso são os mesmos
nos dois idiomas. Uma instalação sem preferência salva começa em PT-BR.

Os recursos em inglês continuam como base canônica. O dicionário PT-BR é aplicado
ao desenhar o texto, incluindo números e mensagens formatadas; ele acompanha todos
os executáveis e também os recursos dos pacotes, sem depender da extração para funcionar. A fonte original permanece nos caracteres ASCII e os acentos usam uma
fonte complementar. Detalhes em [docs/LOCALIZATION.md](docs/LOCALIZATION.md).

## 🔄 Atualizações pelo GitHub

A consulta na abertura vem ativada e pode ser desligada em Settings. Se uma versão
estável mais recente estiver disponível, aparece um indicador no botão Settings.
Para consultar manualmente, escolha **Check for updates**.

1. Confira a versão disponível e escolha **Download**.
2. Após a validação do pacote, escolha **Install update**.
3. No Windows, o instalador aguarda o jogo fechar, atualiza os arquivos e reabre o jogo.
4. No Android, conclua o instalador do sistema. Se o Android abrir a autorização para instalar apps, permita e selecione Install novamente.
5. No Switch/Linux AppImage, feche e reabra após a substituição. A versão anterior fica guardada ao lado do aplicativo.

Os saves, login do Google e preferências não são substituídos pelos arquivos da
release. Falta de internet não impede jogar. A verificação utiliza **releases
publicadas**, não os artefatos de cada push na `main`. A instalação automática
Linux é destinada ao AppImage; builds nativas de desenvolvimento usam atualização manual.

## ☁️ Cloud save: continuar em outra plataforma

1. **Salve o progresso dentro do jogo** antes de abrir Settings.
2. Entre em **Cloud save - Google Drive → Connect Google Drive**.
3. Abra no celular ou computador o endereço indicado, digite o código e autorize a mesma conta Google nas plataformas que pretende usar.
4. No aparelho de origem, escolha **Upload saved progress** e confirme a substituição do backup da nuvem.
5. No aparelho de destino, consulte a data em **Refresh cloud backup**, escolha **Restore cloud backup** e confirme.
6. O jogo **volta ao título no mesmo aplicativo**. Escolha o slot para continuar o progresso restaurado.

O arquivo `heroes_lore_5_save.json` fica na pasta **Heroes Lore - 5** do Drive.
É exclusivo do HL5: não mistura nem converte os saves de HL1, HL2 ou Zero.
A cópia contém somente os arquivos de `internal/` e `external/`; o login e as
preferências de tela ficam no aparelho. Progresso ainda não salvo dentro do jogo
não entra no backup. A sincronização é manual para você escolher qual progresso
enviar ou restaurar.

Antes de restaurar, o aplicativo valida o formato e os checksums. A versão local
anterior fica em `data/save-backups/` (no Android/Linux, dentro da raiz de dados
própria da plataforma). A restauração libera o estado antigo antes de trocar os arquivos e recarrega
os slots no título, evitando que o progresso antigo sobrescreva o backup restaurado.

## 🖥️ Tela: Auto, Wide e Native

| Modo | Área renderizada |
| --- | --- |
| **Auto — padrão** | Expande a cena conforme a janela/tela; acompanha redimensionamento e orientação. |
| **Wide** | Campo de visão próximo de 16:9, com resolução interna de 712×400. |
| **Native** | Campo de visão original, 240×400, com bordas quando necessário. |

Sprites mantêm a proporção: o jogo desenha mais cenário em vez de esticar a imagem.
Alterne em **Configurações → Proporção de tela**, por **F9**, ou segurando
**Select + Start** (**− e +** no Switch). A preferência fica salva.
**F11 / Alt+Enter** alterna tela cheia no PC. O bloqueio de rotação do Android é respeitado.

Mapas pequenos continuam limitados ao cenário original. A resolução interna tem
limite de 1280 pixels por dimensão para controlar memória e custo de desenho.
`hl5.exe --aspect auto` força o modo naquela execução sem sobrescrever a preferência.
Detalhes: [docs/DISPLAY.md](docs/DISPLAY.md).

## 🎮 Controles

| Ação | Teclado | Controle Xbox / padrão | Nintendo Switch |
| --- | --- | --- | --- |
| Mover | WASD / setas | D-pad / analógico esquerdo | D-pad / analógico esquerdo |
| Atacar / confirmar / interagir | Enter, Espaço, Z ou J | A | **A** |
| Cancelar / menu / status | Esc, X, Backspace ou K | B | **B** |
| Habilidade 1 | Q, U ou 1 | X | **X** |
| Habilidade 2 | E, I ou 3 | Y | **Y** |
| Habilidade 3 | R, O ou 7 | LB | L |
| Poção / item | T, P ou 9 | RB | R |
| Trocar fileira de habilidades (`#`) | **C / Tab / #** | **Select / Back** | **−** |
| Mapa | M ou 0 | L3 | L3 |
| Menu original | Esc | Start | + |
| Settings do port | F10 | R3 | R3 |
| Alternar aspect ratio | F9 | Select + Start | − e + |
| Tela cheia (PC) | F11 / Alt+Enter | — | — |

Select/Start sozinhos atuam ao soltar; a combinação de aspect ratio consome os dois
botões e não troca habilidades nem abre o menu. Controles Nintendo Switch Pro
reconhecidos no PC também usam os rótulos Nintendo. Toque/clique e arraste para o
direcional virtual, ou use os botões da interface do jogo.

## 🛠️ Compilar e validar

Windows requer MSYS2 UCRT64, GCC, CMake, Ninja e SDL2. O cliente de rede usa WinHTTP.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python tools/verify_runtime.py --exe build/hl5.exe
python tools/verify_aspect.py --exe build/hl5.exe
python tools/verify_services.py
python tools/verify_localization.py --exe build/hl5.exe
python tools/verify_ui.py --exe build/hl5.exe
python tools/bump_version.py --check
```

No Linux, instale `build-essential cmake ninja-build libsdl2-dev libcurl4-openssl-dev`
e substitua o executável dos testes por `build/hl5` (`python3` se necessário).
No Switch, use devkitPro/devkitA64 com SDL2, curl e mbedTLS; execute
`python3 tools/prepare_switch_romfs.py` e `make -f Makefile.switch`.
No Android, execute `python3 tools/prepare_android_assets.py` e
`cd android && ./gradlew assembleRelease` com SDK/NDK configurados.

O Actions compila as quatro plataformas. Windows/Linux executam a validação ARM,
VFS, áudio e imagens; verificam expansão real da cena, 60 trocas de resolução,
backup/restauração com serviços simulados, controles e empacotamento sem dados
pessoais. No Windows, o instalador também é testado com sucesso, rollback e
rejeição de caminhos inválidos. Esses testes não acessam a conta Google do jogador.

## 🏷️ Versionamento e releases

O script mantém a versão do atualizador, o nome/código Android e os metadados
Switch em acordo. Não cria commit, tag ou push automaticamente.

```sh
python tools/bump_version.py --current
python tools/bump_version.py patch --dry-run
python tools/bump_version.py patch
# Também aceita minor, major ou uma versão explícita: 1.2.0
```

Depois de validar as mudanças, publique a tag correspondente `vX.Y.Z` no GitHub.
O Actions compila as quatro plataformas e publica os seis arquivos da release;
só então os dispositivos podem detectar a atualização.

## 📚 Continuidade do projeto

| Documento | Conteúdo |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Guia de engenharia para pessoas e IAs |
| [docs/HANDOFF.md](docs/HANDOFF.md) | Estado atual e pontos para continuar |
| [docs/RUNTIME.md](docs/RUNTIME.md) | CPU, áudio, desempenho e diagnóstico |
| [docs/DISPLAY.md](docs/DISPLAY.md) | Expansão real de tela e câmera |
| [docs/SERVICES.md](docs/SERVICES.md) | Atualizador, Google Drive, saves, versões e testes |
| [docs/LOCALIZATION.md](docs/LOCALIZATION.md) | PT-BR/inglês ao vivo, fontes, corpus e verificação |

A base coreano → inglês usa `data/dict.bin` e `assets_override/`. O PT-BR usa
`assets/locales/pt-BR.json`, com cache editorial em `lang/pt_br/phrases.json`.
Os scripts `tools/translate_all.py` e `tools/translate_gameplay_tables.py`
mantêm o pipeline de tradução existente. O código principal está em `src/`;
`codex_lab/` é apenas laboratório/backup local e não alimenta releases.

O desempenho foi melhorado com sete regiões ARM recompiladas estaticamente,
mixer SDL2 Ogg/MP3, índice VFS e correção do relógio. O benchmark original de PC
mediu melhora média de 3,13× no processamento do loop; isso não representa uma
promessa de FPS em todos os aparelhos. Métricas e limites estão em `docs/RUNTIME.md`.

Heroes Lore e seus recursos originais pertencem aos respectivos titulares.
Dependências mantêm suas licenças: stb, nlohmann/json, fonte Inter e certificados
de autoridades públicas. Consulte os avisos em `src/vendor/` e `assets/`.
