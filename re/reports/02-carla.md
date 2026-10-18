# RE 02 — Carla (host de plugins)

`carla-git` **2.6.0-alpha1** (AUR), Python 3.14.8, **Qt 5.15.18**.

## Anatomia
| Camada | Arquivos | Papel |
|---|---|---|
| Frontend Python | `/usr/share/carla/*.py` (**92 arquivos**) | UI e lógica: `carla_app.py`, `carla_frontend.py`, `carla_widgets.py`, `carla_skin.py`, `patchcanvas/` |
| Backend (API do cliente) | `carla_backend.py` | interface usada pelo frontend para falar com o engine |
| Host in-process | `carla_host.py` + `libcarla_host-plugin.so` | carrega plugins LV2/VST na própria UI (modo "rack") |
| Controle remoto | `carla_host_control.py` (+ `carla-control`) | servidor/cliente **OSC** |
| Engine C++ | `libcarla_standalone2.so` (3,2 MB) | motor de áudio, grafo, JACK/ALSA/PipeWire |
| Bridges (out-of-process) | `carla-bridge-lv2.so` (2,4 MB), `libcarla_native-plugin.so` | rodam plugins 32/64-bit, Wine, outros toolkits |
| Interposers | `libcarla_interposer-jack-x11.so`, `-x11.so`, `-safe.so` | interceptam JACK/X11 para hospedar plugins que não colaboram |

## Modos de execução (os "apps" que o Carla é)
`carla-rack` · `carla-patchbay` · `carla-jack-multi` · `carla-jack-single` · `carla-control` (remoto) · `carla-settings`

## Formatos hospedados (contagem de referências no código)
`LV2` (17) `VST2` (12) `VST3` (10) `CLAP` (8) `LADSPA` (10) `DSSI` (11) `AU` (6) `JACK` (6)
`SF2` (12) `SFZ` (11) `GIG` (4) `INTERNAL` (8)

## Ponto de integração (o que o nosso app vai usar)
- **OSC com porta TCP e UDP** no engine (`PLUGIN_…`/`get_host_osc_url_tcp`, `CARLA_OSC_*`):
  o `carla-control` é só um cliente OSC. Então dá para controlar rack/patchbay **sem tocar no código**.
- Se a Fase 4 (binário único) for em frente, as interfaces a absorver são: `carla_backend.py` (cliente),
  `carla_host.py` + `carla_host_control.py` (hospedagem e servidor OSC) e o engine `libcarla_standalone2.so`
  (que já é um `.so` reutilizável, não um executável).

## Achado arquitetural crítico
Cada peça usa um toolkit **diferente**:
| Peça | Toolkit |
|---|---|
| alsa-scarlett-gui | **GTK4** |
| Carla | **Qt 5** |
| Ardour 9 | **ytk/ydk** (fork próprio) + bindings GTK2 |
| Nosso painel (proposto) | GTK4 (igual ao overlay/alsa-scarlett-gui) |

Consequência: **embutir as janelas deles não é viável** (nem XEmbed, nem subsurface Wayland entre toolkits).
O caminho é orquestrar: nosso app é o plano de controle (OSC + ALSA ctl + PipeWire), e cada peça roda no seu
próprio processo com o seu próprio toolkit. Isso reforça o desenho do README (Fase 3) e explica por que a
Fase 4 é "meses": significaria carregar três toolkits no mesmo processo.
