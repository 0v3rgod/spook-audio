# SPOOK AUDIO — projeto de unificação do stack de áudio

Pedido do Taylor (12/10/2026): *"fazer engenharia reversa nisso tudo... depois juntar tudo num app só:
ardour, ALSA Scarlett, Carla, fazer funcionar em JACK, ALSA, PipeWire"*.

## Decisões
- **Fases 0-3 aprovadas** (RE → conserto da base → integração numa DAW → painel unificado).
- **Fase 4 (binário único de verdade)** aprovada como objetivo final, mas só depois das fases anteriores.
- Pacotes extras instalados **só para a RE**; serão **desinstalados** no fim (ver `re/INSTALADOS-PARA-RE.md`).

## Arquitetura (a unificação real)
```
                    ┌──────────── SPOOK AUDIO (GTK4) ────────────┐
                    │  painel: patchbay, transporte, hardware     │
                    └───────┬──────────────┬───────────────┬─────┘
              OSC/3819     │   OSC        │   ALSA ctl    │  JSON (pw-dump)
                    ┌───────▼──────┐  ┌────▼─────┐  ┌──────▼──────┐
                    │  Ardour 9.8  │  │  Carla   │  │  PipeWire   │
                    │  (DAW/grav.) │  │ (plugins)│  │  (núcleo)   │
                    └──────────────┘  └──────────┘  └──────┬──────┘
                                                          │
                             pipewire-alsa / pipewire-jack / pipewire-pulse
                                                          │
                                             ┌────────────▼───────────┐
                                             │ Scarlett Solo 4th Gen  │
                                             └────────────────────────┘
```
PipeWire **é** ALSA + JACK + Pulse ao mesmo tempo: ninguém precisa ser "portado".
Backend por app: `pw-jack ardour9` (JACK) · `ardour9` (ALSA) · nativo (PipeWire).

## Fases
- **Fase 0 — RE** — concluída.
  - [x] `re/reports/01-scarlett-protocolo.md`
  - [x] `re/reports/02-carla.md`
  - [x] `re/reports/03-ardour.md` — **motor mora na sessão** (`<EngineHints>`), OSC vem
        **desligado** e `/heartbeat` não existe (a sonda certa é `/group/list`)
  - [x] `re/reports/04-pipewire.md`
- **Fase 1 — conserto da base**: o Ardour abre e cria sessão (validado; sob Xvfb *sem*
  window manager ele para em "Detecting Audio/MIDI Devices" — limitação do Xvfb, não do Ardour).
- **Fase 2 — integração numa DAW**: Carla como rack + sessão modelo. **Kit pronto**
  (os dois arquivos são gerados e validados no disco); a execução ao vivo
  (`--executar`) **não foi rodada de propósito** para não interromper o áudio que
  estivesse tocando — ver "Fase 2" abaixo.
- **Fase 3 — painel SPOOK AUDIO** — **funcionando**: 5 abas (SESSÃO, SCARLETT,
  PATCHBAY, FASE 2, FERRAMENTAS) + transporte OSC.
- **Fase 4 — binário único** — **PoC FUNCIONANDO (14/10/2026)**: um binário nosso
  (`fase4/engine/spook-audio-engine`) linka o `libardour` do pacote `ardour`
  **sem header de GUI**, sobe o motor, cria/abre sessão, cria trilha, lê as rotas e
  encerra limpo — com backend **Dummy** (nada de som tocado). Detalhes, o bug de ABI
  de 16 bytes que custou o dia e o `tools/abi-check.sh` em **`fase4/PoC-ENGINE.md`**.
  **Consolidada em 17/10/2026 como SPECTER** com o runtime do Ardour EMBUTIDO
  (`fase4/vendor/ardour/`) — o pacote `ardour` não é mais necessário em runtime
  nem para compilar. Ver "SPECTER — o estado final" no fim deste README.

## Fase 2 — como ficou

O que a Fase 2 tem de resolver (RE 03): o Ardour **não aceita o motor pela linha de
comando** e o Carla **não aceita a rack pela linha de comando** — os dois gravam tudo
no arquivo deles. Então "fase 2" = **gerar esses arquivos** de forma reprodutível:

| peça | quem gera | o que escreve |
|---|---|---|
| sessão modelo do Ardour | `spookaudio/sessao.py` | `.ardour` com `<EngineHints backend="JACK">` + OSC ligado |
| rack do Carla | `spookaudio/projeto.py` | `.carxp` no formato do próprio engine (`CarlaEngine::saveProjectInternal`) |

```bash
python3 -m spookaudio.fase2                 # dry-run: gera + mostra o plano (nada executa)
python3 -m spookaudio.fase2 --executar      # sobe Carla rack + Ardour + patch (opt-in)
```

O dry-run é o padrão: ele **escreve os arquivos, lê o grafo do PipeWire** (`pw-dump`,
só leitura) e imprime o `pw-link` que faria. Nada é conectado sem `--executar`, e nem
o painel nunca conecta (aba FASE 2 = botões de preparar/validar/ler o plano).

Decisões que valem registrar:
- **sessão modelo não cria trilhas**: um `<Route>` escrito na mão é frágil; o painel
  cria as trilhas na sessão já aberta com OSC (`Track/NewAudioTrack`, já existente).
- **o roteamento é `pw-link`**, não o patchbay interno do Carla: quem manda no grafo
  aqui é o PipeWire (RE 04).
- **plugins do rack**: não há plugin de terceiros instalado nesta máquina (o LSP/Calf
  veio junto com a RE e foi removido — `re/INSTALADOS-PARA-RE.md`), então o rack se
  apoia nos plugins **INTERNAL** do Carla, que sempre existem (`bigmeter`, `audiofile`,
  …). O `Medidor` é o invólucro; um processador real entra no mesmo slot depois.
- **pendente**: o OSC de controle do Carla (`/ctrl/<método>` e `/<alvo>/<id>/<método>`,
  do cliente oficial `carla_host_control.py`) não foi ligado — a rack é controlada
  pelos arquivos (`projeto.py`), não por OSC ao vivo.


## Fase 4 — a UI (2026-10-15): cara de DAW de verdade

O esqueleto da Fase 4 aparecia "sem cara de software de gravação". A `main_window`
foi reescrita como uma DAW escura (tema via `Gtk::CssProvider`, sem asset externo):
cabeçalho com LED do motor, transporte redondo (rew/play/stop/**rec**/pânico),
medidor de CPU, abas estilo console, cartões, **tiras de canal** com fader +
comutador, badges de tipo de plugin e barra de status.

- **Arquivos**: `fase4/spook/ui/main_window.{h,cc}` (backup da versão anterior:
  `main_window.{h,cc}.bak-20261015`). Continua **gtkmm-3.0** (regra de ABI:
  `-DWAF_BUILD`, ver `fase4/PoC-ENGINE.md`).
- **Nada toca som**: o backend continua `None (Dummy)`. Os faders da SCARLETT
  começam **READ-ONLY**; só escrevem no mixer quando o botão **ESCRITA** é ligado
  (o botão fica rosa e o selo muda para `ESCRITA LIBERADA`; as tiras ficam com
  realce ciano). Sem placa Scarlett conectada, o painel escolhe o cartão com mais
  controles (hoje o `sofhdadsp`) para as tiras não ficarem vazias.
- **Debug**: `SPOOK_TAB=0..4` abre direto na aba, `SPOOK_NON_UNIQUE=1` permite
  instâncias, `SPOOK_DUMP_TREE=1` despeja a árvore de widgets.
- **Verificação sem tocar a tela dele**: `Xvfb :99` + `env -u WAYLAND_DISPLAY
  GDK_BACKEND=x11` (o GDK prefere Wayland quando `WAYLAND_DISPLAY` existe).
  Foi conferido por clique real (XTest): as abas trocam e o **ESCRITA** liga/desliga
  (medido por cor: realce ciano `#34e1e1` + slider claro `#dfe4ee` só quando ligado).
- **WM_CLASS**: instance `spook-audio`, class `Spook-audio`.

## Fase 4 — funções do REAPER (2026-10-17)

Evolução da UI acima: além de "cara" de DAW, funções de verdade, no estilo do
REAPER. **Nada foi reimplementado**: transporte, marcadores, undo/redo e save
usam a classe **`BasicUI`** do próprio Ardour (de `libardourcp.so`, a mesma que o
OSC usa) e o resto fala direto com `ARDOUR::Session`/`Route`.

**Núcleo novo**: `fase4/spook/core/daw.{h,cc}` (`Daw`) — transporte
(play/stop/roll/rec-arm/loop/click/goto start-end/rew-ffwd/jump por compasso/
pânico MIDI), mapa de tempo (BPM + fórmula), locais (listar/criar/remover/ir),
undo/redo/save, mixer por rota (gain/pan/mute/solo/arm/peak) e trilhas
(audio/MIDI/remover).

**UI** (`fase4/spook/ui/main_window.{h,cc}`):
- **Toolbar** estilo REAPER: `+TRILHA · +MIDI │ MARCADOR · LOOP · CLICK │
  DESFAZER · REFAZER · SALVAR │ AÇÕES · PÂNICO MIDI` + LED de estado.
- **Transporte**: `MOTOR ▶/■` + cluster redondo `|◀ ◀◀ ▶ ■ ● ▶| ⊘` + **LCD**
  (`BARRAS·BATIDAS`, `TEMPO`, `AMOSTRAS`) + **BPM** editável + fórmula de
  compasso + medidor de CPU.
- **MIXER** (aba 0): uma **tira de canal** por rota — nome, badge
  AUDIO/MIDI/MASTER, pan com valor `L…/C/R…`, **fader vertical em dB**,
  **medidor VU em cairo** (verde -60..-18 / amarelo -18..-6 / vermelho -6..0 +
  peak hold; clique zera o hold) e `M`/`S`/`●`(arm). O **master** entra no fim.
- **Track Headers — referências Logic Pro + Studio One (2026-10-17)**: o nome
  da faixa agora é um `Entry` editável no topo de **cada strip do MIXER** e na
  **Track List** da aba SESSÃO. Clique, edite e confirme com **Enter** (ou
  troque o foco). Isto usa `Track::set_name`/`Route::set_name` do Ardour: não é
  apelido visual — renomeia I/O, toma/nome de disco e persiste na sessão. Uma
  faixa armada é corretamente recusada pelo core até ser desarmada. Cada strip
  também ganhou seletor de **cor persistente** (`PresentationInfo::color`), e a
  Track List ganhou **DUP**: duplica tipo, nome, parâmetros de mix e cor (não
  duplica conteúdo/áudio nesta fase). `F2` abre a Track List para renomear.
- **ARRANGE VIEW — referências Logic Pro + Studio One (2026-10-17)**: nova aba
  **ARRANJO**, com régua de compassos, lanes reais das rotas, playhead rosa,
  grid, Track List lateral com tipo/cor e zoom horizontal de 8–128 compassos.
  Clique na régua/lane para localizar o cursor. `+ SEÇÃO` cria blocos globais
  de Intro/Verso/Refrão/Ponte, usando `Location::IsRangeMarker` do Ardour —
  portanto as seções são persistentes, aparecem também em MARCADORES & REGIÕES
  e entram na sessão salva. Isto traz o **Arranger Track** de Studio One sem
  fingir que uma seção é áudio/MIDI.
- **Clips MIDI reais (2026-10-17)**: `+ CLIP MIDI` cria uma fonte MIDI vazia,
  uma `Region` e a adiciona à `Playlist` da trilha MIDI escolhida; é material
  persistente do Ardour, não um desenho. No canvas ele aparece na lane correta;
  clique para selecionar (contorno rosa), arraste o corpo para mover com snap de
  compasso, arraste os últimos 10 px da direita para redimensionar, ou use
  `DUP CLIP` / `APAGAR CLIP`. O núcleo usa `RegionFactory::create`,
  `Playlist::add_region/remove_region` e `Region::set_position/set_length`.
- **Clips de áudio reais (2026-10-17)**: o bloco `AUDIO CLIP` permite escolher
  uma trilha de áudio e importar WAV/FLAC/AIFF/MP3/OGG. A implementação abre
  uma `AudioFileSource` externa, constrói uma `AudioRegion` via
  `RegionFactory` e a coloca na Playlist exatamente no cursor. O canvas lê
  amostras da `AudioSource` apenas para desenhar uma mini-waveform — isso não
  arma entrada, não inicia o transporte e não toca som. Os mesmos controles de
  selecionar, mover, redimensionar, duplicar e apagar usados pelo MIDI valem
  para o clip de áudio.
- **Piano Roll material (2026-10-17)**: com um clip MIDI selecionado, `PIANO
  ROLL` abre o editor de notas em duas oitavas (C3–B4). Clique no grid para
  criar uma nota de 1 beat, arraste o corpo para movê-la, arraste a borda
  direita para mudar sua duração, ou use botão direito para apagá-la. Não é
  uma lista visual: lê e altera o `MidiModel` do `MidiRegion`; cada mutação usa
  `MidiModel::NoteDiffCommand` e
  `apply_diff_command_as_commit(Session, ...)`, portanto grava no `.mid`,
  persiste na sessão e `Ctrl+Z`/`Ctrl+Y` do Ardour funciona. A janela só abre
  por ação do usuário — nunca nos testes/uso normal sem pedido explícito.
- **LISTA DE AÇÕES** (aba 3): a "action list" do REAPER — 38 ações com ID,
  descrição e **atalho**, filtrável, executada com duplo clique/Enter.
- **MARCADORES & REGIÕES**: na aba SESSÃO — listar, inserir no cursor, remover o
  do cursor e pular para o local.
- **Atalhos**: `espaço` play/pause, `R` arm-rec, `L` loop, `M` marcador,
  `Del` remove marcador, `Home`/`End`, `Ctrl+Z/Y/S`. Não rouba tecla quando o
  foco está num campo (Entry/TextView/Scale/TreeView/SpinButton).

### Bugs reais achados e corrigidos nesta rodada (com a medição)
1. **Ganho em coeficiente, não em dB** — `gain_control()->get_value()` devolve
   **coeficiente** (1.0 = unidade). O fader mostrava `+1,0 dB` num canal em 0 dB
   e o `set_gain` gravava dB no controle. Corrigido com
   `accurate_coefficient_to_dB()` / `dB_to_coefficient()` (`ardour/dB.h`).
   Confirmado no dump: `+0,0 dB`.
2. **Pan com alcance fixo** — o panner **mono** vai de **0 a 1** (centro 0,5) e o
   estéreo de -1 a 1; assumir -1..1 fazia o centro sair `R50`. Agora o alcance
   vem de `pan_azimuth_control->lower()/upper()` e o rótulo normaliza pelo centro
   (`pan_txt`). Confirmado: `C`.
3. **O Ardour NÃO cria o bus master sozinho** — quem chama
   `Session::add_master_bus()` é o GUI. Sem ele não havia canal master no mixer
   nem soma. `Daw::ensure_master_bus()` (idempotente) é chamado no `sync_daw()`;
   `ordered_routes()` (usada por `route_at`/`strips`/`route_names`) põe o master
   no fim para o índice do fader não apontar para outra rota.
4. **SEGV no encerramento** — ao **dispor** o notebook o GTK emite
   `switch-page`; como os membros `_engine`/`_daw` morrem antes do
   `~Gtk::Widget`, o handler `refresh_routes()` lia um `Engine` já destruído
   (`SEGV_MAPERR` em `list<shared_ptr<Route>>::begin`). O destrutor agora
   desconecta `_sig_page`.
5. **Laço infinito do LOOP → `POOL OUT OF MEMORY`** — `refresh_transport()`
   fazia `_tb_loop->set_active(...)`, o que emite `toggled` → `on_loop()` →
   alterna → `set_active()`… até estourar o pool do PBD
   (`[FATAL] CRITICAL: POOL OUT OF MEMORY`). Corrigido com `_toggle_guard`
   (mesmo padrão do `_mixer_updating`/`_bpm_guard`).

### Verificação headless (sem tocar na sessão Wayland dele)
```bash
# variáveis de diagnóstico (nenhuma afeta o uso normal)
Xvfb :99 & 
env -u WAYLAND_DISPLAY DISPLAY=:99 GDK_BACKEND=x11 SPOOK_NON_UNIQUE=1 \
    SPOOK_AUTO_SESSION=1 SPOOK_TAB=0 SPOOK_DUMP_TREE=1 ./fase4/app.sh
```
- `SPOOK_AUTO_SESSION=1` sobe o motor (Dummy) + abre uma sessão em
  `/tmp/opencode/spook-ui-dump` + cria trilha de áudio e MIDI + marcador +
  seções `Intro`/`Verso` e um clip MIDI `Beat 01`, para o dump mostrar mixer e
  Arranger com conteúdo real.
- `SPOOK_TAB=0..7` escolhe a aba; `SPOOK_DUMP_TREE=1` despeja a árvore e sai.
  O dump imprime também a **alocação** de cada widget (`[LxA @x,y]`), então dá
  para conferir geometria sem abrir janela: medido no MIXER, cada tira sai com
  `100x568` contendo fader vertical `40x425` e dois medidores VU `10x425`.
- `SPOOK_KEYTEST=1` exercita os **atalhos** com `GdkEventKey` sintéticos e loga
  o estado resultante (`rolling/arm/loop/locais`). Resultado: 15/15 corretos,
  inclusive "com o foco num Entry o atalho é ignorado".
- Medido: as 8 abas com `RC=0` (e `[FATAL]` = 0) e `--self-test: TUDO OK`.
  O self-test também cria uma trilha e a renomeia via `Daw::rename_route()`;
  confirmação real: `Trilha de Teste -> Trilha Renomeada`.

**OSC**: a Fase 4 **não abre porta OSC** (usa `BasicUI` em vez de superfície OSC),
então `tests/osc_probe.py` vale para a Fase 3, não para este binário.

## Como rodar
```bash
~/spook-audio/bin/spook-audio            # ou: python3 ~/spook-audio/spook-audio.py
python3 ~/spook-audio/spook-audio.py --check                 # autoteste (16 casos)
env -u WAYLAND_DISPLAY GDK_BACKEND=broadway gtk4-broadwayd -a 127.0.0.1 :7 &
BROADWAY_DISPLAY=:7 GDK_BACKEND=broadway python3 -m tests.selftest   # inclui a janela
```
O botão **Testar OSC** só responde com o Ardour rodando **e** o OSC ligado — o painel
tem o botão **Ligar OSC** justamente porque ele vem desligado de fábrica.

**Regra de convivência**: o painel só abre quando o usuário manda, e o teste da janela
usa o backend **Broadway** (`gtk4-broadwayd`) ou Xvfb — nunca a sessão Wayland dele.

## Estrutura
```
~/spook-audio/
  README.md                 este arquivo
  spook-audio.py bin/       ponto de entrada do painel
  spookaudio/               o app: app.py (GTK4) + alsa/pw/scarlett/ardour/carla/osc
                            projeto.py (.carxp) · sessao.py (.ardour) · fase2.py (Fase 2)
  spookaudio/data/          template da sessão modelo (placeholders @NOME@/@UUID@/…)
  tests/selftest.py         autoteste headless (16 casos, inclui grafo sintético)
  re/kernel/                driver do kernel + UAPI (fonte de verdade do protocolo)
  re/alsa-scarlett-gui/     GResource extraído (CSS/ícones) + strings REA
  re/carla/ re/ardour/ re/pipewire/
  re/reports/               relatórios de engenharia reversa
  re/INSTALADOS-PARA-RE.md  lista do que instalar/desinstalar
  fase4/                    SPECTER — BINÁRIO ÚNICO (gtkmm-3.0 + libardour)
    app.sh                  launcher (usa vendor/ardour quando existe), --self-test
    build-app.sh            build (exige -DWAF_BUILD; ver PoC-ENGINE.md)
    spook/main.cc           modos CLI + launcher da GUI
    spook/core/engine.{h,cc}   motor (libardour), backends, sessão + paths do vendor
    spook/core/daw.{h,cc}      transporte/mapa de tempo/locais/mixer/tracks
    spook/core/plugins.{h,cc}  catálogo de plugins (cache)
    spook/core/scarlett.{h,cc} mixer ALSA (READ-ONLY por padrão)
    spook/ui/main_window.{h,cc} a janela (toolbar, transporte/LCD, MIXER, AÇÕES)
    vendor/ardour/          RUNTIME DO ARDOUR EMBUTIDO (lib/share/etc — ~100 MB)
    tools/vendor-ardour.sh  (re)copia o runtime do pacote `ardour` para o vendor
    src/ardour/             árvore de fontes do Ardour (headers p/ compilar)
    PoC-ENGINE.md           o bug de ABI dos 16 bytes + tools/abi-check.sh

## SPECTER — o estado final (2026-10-17): DAW autônoma

O app (nome visível **SPECTER**, app id/WM class `com.taylor.specter`, atalho
`SUPER+SHIFT+E`) não depende mais de nenhum app de terceiro em runtime:

- **Runtime do Ardour embutido**: `fase4/vendor/ardour/`
  (`lib` = /usr/lib/ardour9 inteiro; `share` = /usr/share/ardour9 menos
  `media/` e `web_surfaces/`; `etc` = /etc/ardour9). O binário sai com
  `DT_RPATH = $ORIGIN/vendor/ardour/lib` — com `-Wl,--disable-new-dtags` de
  propósito, senão o RUNPATH não valeria para as dependências INDIRETAS do
  libardour. O `spook/core/engine.cc` define sozinho `ARDOUR_DLL_PATH/
  ARDOUR_DATA_PATH/ARDOUR_CONFIG_PATH/VAMP_PATH` a partir de
  `/proc/self/exe`: mover a pasta do projeto não quebra nada.
- **Prova de autonomia** (pacote `ardour` invisível — `unshare -rm` + tmpfs em
  /usr/lib/ardour9, /usr/share/ardour9 e /etc/ardour9): `--self-test` =
  `TUDO OK` (binário direto e via `app.sh`); UI headless completa nas abas
  MIXER, ARRANJO (seções Intro/Verso reais) e PLUGINS (catálogo ACE
  EQ/Compressor/Delay/Reverb/Fluid Synth/Reasonable Synth saindo do vendor)
  com `rc=0`. Ordem de desinstalação em `fase4/PACOTES-PARA-REMOVER.txt` —
  o pacote `ardour` já pode sair; `boost` fica só para COMPILAR.
- **Build**: `cd fase4 && ./build-app.sh && ./app.sh --self-test` (gtkmm-3.0,
  alsa, libxml2 + headers do boost; nunca gtkmm-4 — regra de ABI do
  `PoC-ENGINE.md`). Atualizar o runtime embutido depois de um upgrade do
  Ardour: `./tools/vendor-ardour.sh && ./build-app.sh`.
- **GitHub**: repo privado `0v3rgod/spook-audio` — o `vendor/` entra
  commitado de propósito: é o que torna o clone autossuficiente de verdade.
```
