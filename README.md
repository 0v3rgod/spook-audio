# SPOOK Audio (SPECTER)

Unificação do stack de áudio Linux: **PipeWire** (núcleo), **JACK** (baixa latência), **ALSA** (driver Scarlett), **Ardour** (DAW/gravação), **Carla** (plugins LV2/VST). Interface única em **GTK4** + **C++** (Meson) com painel Python auxiliar.

## O que é

Um app de áudio profissional que roda sobre o motor do Ardour (linka `libardour`), expõe patchbay, transporte, controle de hardware (Scarlett Solo 4th Gen via ALSA UAPI) e descoberta de plugins Carla — tudo em uma janela GTK4 nativa, sem depender de DAWs externas.

## Stack

| Camada | Tecnologia |
|--------|------------|
| Núcleo de áudio | PipeWire + JACK (via `pipewire-jack`) |
| Driver de hardware | ALSA UAPI (`mixer_scarlett2.c` do kernel) |
| DAW / Engine | Ardour 8.x (linka `libardour` via `pkg-config`) |
| Plugins | Carla (LV2/VST3), `liblilv`, `lv2` |
| UI | GTK4 (C++), Meson build |
| Painel auxiliar | Python + `python-socketio` (status, logs) |

## Requisitos

- Arch Linux (ou derivado) com PipeWire, JACK, Ardour 8.x, Carla, `liblilv`, `lv2`, GTK4, Meson ≥ 1.2
- Scarlett Solo 4th Gen (ou compatível com driver `scarlett2`)
- Python 3.10+ com `python-socketio`, `python-engineio`

## Build

```bash
# 1. Dependências (Arch)
sudo pacman -S pipewire jack2 ardour carla liblilv lv2 gtk4 meson base-devel pkgconf

# 2. Clone e build
git clone https://github.com/0v3rgod/spook-audio.git
cd spook-audio
meson setup builddir
meson compile -C builddir

# 3. Executa
./builddir/spook-audio
```

## Estrutura

```
spook-audio/
├── fase4/                 # App principal C++ (GTK4 + libardour)
│   ├── src/               # Código fonte (main_window, patchbay, transport, hardware)
│   ├── meson.build        # Build Meson (linka libardour, gtk4, lilv, lv2)
│   └── assets/            # Ícones (16x16..128x128) + specter-icon-rounded.png
├── spookaudio/            # Painel Python (status, logs, IPC via socket)
│   └── app.py
├── scripts/               # Helpers (device probe, jack/pipewire utils)
└── .gitignore             # Ignora vendor/, re/, builddir/, *.pyc
```

## Código de terceiros (NÃO versionado)

O repositório **não** inclui:
- `vendor/` — runtime do Ardour (~100 MB, GPL)
- `re/` — código do driver kernel `scarlett2` (GPL) + assets/docs do `alsa-scarlett-gui` (GPL)

**Como obter:**
- `libardour` + headers: `sudo pacman -S ardour` (fornece `pkg-config --cflags --libs libardour`)
- Driver `scarlett2`: já no kernel Linux ≥ 6.6 (`snd-usb-audio` + `mixer_scarlett2`)
- `alsa-scarlett-gui`: `git clone https://github.com/alsa-project/alsa-scarlett-gui` (GPL-2.0)

Como o binário linka `libardour` (GPL), **o projeto é compatível com GPL-3.0**. O arquivo `LICENSE` reflete isso.

## Licença

GPL-3.0-or-later — o binário linka `libardour` (GPL). Código próprio sob GPL-3.0; dependências de terceiros mantêm suas licenças (GPL/LGPL/MIT).
