# Instalados só para a engenharia reversa — ✅ REMOVIDOS em 12/10/2026 23:03

> **Resultado**: os 24 pacotes foram removidos com `-Rns` (rc=0). **Cuidado aprendido**: o
> `carla-git` (que NÃO estava na lista) foi levado junto porque havia sido marcado como
> **dependência** (`asdeps`) em 31/07 — o `-Rns` o tratou como dependência órfã. Foi
> reinstalado pelo AUR e marcado **explícito** (`pacman -D --asexplicit carla-git`), o mesmo
> para `suil` e `qt5-x11extras` (dano colateral, também restaurados). **Antes de qualquer
> `-Rns` futuro: conferir `pacman -Qi <pkg> | grep "Instalado como"`.**

# (histórico) Instalados só para a engenharia reversa (remover no fim)

Baseline gravado **antes** da instalação: `/tmp/opencode/pacman-explicit-before.txt` (157 pacotes explícitos).
Lista do que foi instalado **para a RE**: `/tmp/opencode/audio-re-install-list.txt` (cópia abaixo).

```
lsp-plugins lsp-plugins-lv2 calf zam-plugins guitarix a2jmidid qjackctl vmpk audacity
hydrogen drumgizmo sfizz surge-xt helm yoshimi setbfree soundfont-fluid easyeffects
drumkv1 samplv1 qtractor musescore
```
Log da instalação: `/tmp/opencode/pacman-audio-install.log` (rc=0, 22 pacotes + dependências).

## Desinstalar no fim (comando pronto)
```bash
sudo -n pacman -Rns --noconfirm $(cat /tmp/opencode/audio-re-install-list.txt)
sudo -n pacman -Qdt            # confere órfãos deixados pelas dependências
```
`-Rns` remove também as dependências que vieram junto (qt5-x11extras, pd, tcl, tk, zenity, drumstick,
suil, fltk, wxwidgets-*, sfizz-lib, ...) desde que nada mais precise delas.

## O que NÃO pode ser removido (é a base do Taylor, não entrou nesta lista)
`ardour` `carla-git` `alsa-scarlett-gui` `pipewire*` `wireplumber` `qpwgraph` `helvum` `pavucontrol`
`fluidsynth` `jack-example-tools` `gdb` `openbox` `xorg-xinit` `vlc-plugin-alsa`

## Artefatos da RE que ficam depois de desinstalar
`~/spook-audio/re/**` (driver do kernel, UAPI, GResource do GUI, relatórios) — nada disso depende dos pacotes.
