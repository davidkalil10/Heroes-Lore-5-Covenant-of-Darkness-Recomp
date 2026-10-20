# Heroes Lore 5: Covenant of Darkness — Multiplatform Native Recompilation

Projeto de recompilação e port nativo de **Heroes Lore 5: Covenant of Darkness** (EA Mobile Korea) para múltiplas plataformas modernas:
- **PC Windows** (x64)
- **Linux** (x86_64 / Steam Deck / AppImage)
- **Nintendo Switch** (.nro Homebrew)
- **Android** (APK Universal e ARM64)

---

## Atualização de execução e áudio — 20/10/2026

A versão validada pelo autor no PC foi integrada ao código principal: execução
fluida, música e efeitos sonoros funcionando. Windows, Linux, Android e Switch
compartilham as correções de CPU, áudio SDL2 e acesso aos recursos.

- Sete trechos ARM de desenho e decodificação são recompilados estaticamente para
  C++, com validação de opcodes e retorno ao interpretador se o binário mudar.
- Música e efeitos Ogg/MP3 usam mixer SDL2 próprio, sem dependência de SDL2_mixer.
- Um índice dos recursos VFS evita reabrir e percorrer o arquivo em cada consulta.
- O relógio usa tempo real acumulado; quadros lentos deixam de virar câmera lenta.

No benchmark de PC de 1.200 loops, três repetições por versão, o tempo médio caiu
de **9,092 para 2,904 ms (3,13x)**; o percentil 95 caiu de **49,740 para 15,219 ms**.
Esses números medem processamento do loop, não FPS garantido em outras plataformas.
Android e Switch precisam de novos testes nos aparelhos com os artefatos do Actions.

O Actions verifica Windows/Linux contra o interpretador original antes de empacotar:
14 comparações de estado ARM, consultas VFS, 11 imagens idênticas e sinal de áudio.
Para reproduzir depois de compilar:

```bash
python tools/verify_runtime.py --exe build/hl5.exe
# Linux: --exe build/hl5
```

Detalhes de arquitetura e diagnóstico: [docs/RUNTIME.md](docs/RUNTIME.md).
Continuidade entre IAs: [docs/HANDOFF.md](docs/HANDOFF.md).

---

## 🎮 Controles

### Teclado (PC / Linux)
| Ação | Teclas |
| :--- | :--- |
| **Movimento** | `W`, `A`, `S`, `D` ou Setas direcionais |
| **Ataque / Confirmar / Interagir** | `Enter`, `Espaço`, `Z` ou `J` |
| **Cancelar / Menu / Status** | `Esc`, `X`, `Backspace` ou `K` |
| **Skill 1** | `Q` ou `U` (Tecla 1) |
| **Skill 2** | `E` ou `I` (Tecla 3) |
| **Skill 3** | `R` ou `O` (Tecla 7) |
| **Usar Poção / Item** | `T` ou `P` (Tecla 9) |
| **Mapa** | `M` (Tecla 0) |

### Gamepad / Controle (Xbox, PlayStation, Switch Pro)
- **Analógico Esquerdo / D-Pad**: Movimentação fluida em 8 direções
- **Botão A** (Xbox / Nintendo B): Ataque / Confirmar / Interagir
- **Botão B** (Xbox / Nintendo A): Cancelar / Menu / Status
- **Botão X**: Skill 1
- **Botão Y**: Skill 2
- **LB / L1**: Skill 3
- **RB / R1**: Usar Poção / Item
- **Select / L3**: Mapa Rápido
- **R3**: Alternar Poção / Item
- **Start**: Menu / Pausa

### Mouse / Touch
- Clique e arraste na tela para ativar o direcional virtual dinâmico.
- Cliques diretos em botões de interface (Mapa, Loja, Salvar, Habilidades).
- Suporte total a telas sensíveis ao toque (Android, Switch e PCs touch).

---

## 🚀 Como Jogar

### Windows Standalone (Local)
1. Navegue até a pasta `dist/`.
2. Execute `hl5.exe`.
3. O jogo iniciará com tradução em inglês, áudio SDL2 e os trechos ARM otimizados.

### Builds Automatizadas (GitHub Actions)
A cada push na branch `main` ou criação de tags `v*`, o GitHub Actions compila automaticamente para:
1. **Windows**: Arquivo ZIP portátil (`heroes_lore_5_windows_x64.zip`) contendo executável e todas as dependências.
2. **Nintendo Switch**: Arquivo `.nro` (`heroes_lore_5.nro`) pronto para a pasta `/switch/` do SD Card.
3. **Linux**: Pacote AppImage (`heroes_lore_5_linux_x86_64.AppImage`) executável diretamente no Ubuntu, Fedora, Steam Deck, etc.
4. **Android**: APKs assinados (`heroes_lore_5_android_universal.apk` e `heroes_lore_5_android_arm64.apk`).

---

## 🛠️ Compilação Manual

### Windows (MSYS2 UCRT64)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Ubuntu / Debian)
```bash
sudo apt-get install build-essential cmake ninja-build libsdl2-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Nintendo Switch (devkitPro devkitA64)
```bash
python3 tools/prepare_switch_romfs.py
make -f Makefile.switch
```

### Android (Gradle)
```bash
python3 tools/prepare_android_assets.py
cd android
./gradlew assembleRelease
```

---

## 🌐 Sistema de Tradução
O jogo conta com um sistema de tradução dinâmico baseado em dicionário (`data/dict.bin`) e substituição direta de tabelas e diálogos (`assets_override/`).
Para reprocessar ou expandir traduções:
```bash
python tools/translate_all.py
```
