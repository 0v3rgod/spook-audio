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
`ardour` `carla-git` `pipewire*` `wireplumber` `qpwgraph` `helvum` `pavucontrol`
`fluidsynth` `jack-example-tools` `gdb` `openbox` `xorg-xinit` `vlc-plugin-alsa`

### Atualização 18/10/2026: `alsa-scarlett-gui` liberado para remoção
O painel SCARLETT do próprio SPECTER (aba 5) substitui o app: lê todos os
controles do driver scarlett2 (Line In 1/2, Air, Phantom, Direct Monitor,
PCM/mixes) e a escrita foi validada no hardware — resumo em
`fase4/PACOTES-PARA-REMOVER.txt`, item 5. Nada do sistema depende dele
(`Required By: None`, sem autostart) e o SPECTER não referencia nada do pacote
(fala ALSA por `snd_ctl` direto).
```bash
sudo pacman -Rns alsa-scarlett-gui   # --print conferido: sai SÓ ele (o carla não repete)
sudo pacman -Qdt                     # órfãos depois (esperado: nada)
```
Docs do pacote preservadas em `re/alsa-scarlett-gui/docs/` (`demo/*.state` =
catálogo de controles de todos os modelos — inclui `Scarlett Gen 4 Solo.state`
— mais `iface-*.md`, `USAGE.md`, `img/`). Se algum dia faltar (ex.: atualizar
firmware, que o SPECTER não faz): `sudo pacman -S alsa-scarlett-gui`.

## Artefatos da RE que ficam depois de desinstalar
`~/spook-audio/re/**` (driver do kernel, UAPI, GResource do GUI, relatórios) — nada disso depende dos pacotes.
