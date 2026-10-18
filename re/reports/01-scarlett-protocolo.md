# RE 01 — Protocolo da Focusrite Scarlett (Solo 4th Gen)

Alvo do Taylor: **Focusrite Scarlett Solo 4th Gen** — USB `1235:8218`, serial `T1EGXR0380613F`.

## Fontes usadas (o protocolo tem fonte oficial — não precisou decompilar o módulo)
| Fonte | Caminho | O que dá |
|---|---|---|
| UAPI do kernel | `/usr/lib/modules/7.2.9-arch1-1/build/include/uapi/sound/scarlett2.h` (copiado p/ `re/kernel/scarlett2-uapi.h`) | ioctls de hwdep: versão de protocolo, reboot, seleção/erase de setor de flash |
| Driver do kernel (upstream) | `sound/usb/mixer_scarlett2.c` (9.935 linhas → `re/kernel/mixer_scarlett2.c`) | comandos USB, tabela de modelos, layout de canais, nomes de controles, tabelas de roteamento |
| FCP | `sound/usb/fcp.h` (citado no driver) | *Focusrite Control Protocol* — caminho novo, ativado por `SCARLETT2_USE_FCP_DRIVER` |
| ALSA Scarlett GUI | `/usr/bin/alsa-scarlett-gui` (REA/Ghidra: 1.044 procs, 1.008 strings) + GResource extraído | como o painel faz: 4 janelas, VU, routing, updater de firmware |
| Estado por modelo | `/usr/share/doc/alsa-scarlett-gui/demo/*.state` | catálogo real de todos os controles de cada modelo |

## 1. Descoberta do dispositivo
- O kernel casa USB `1235:8218` → `solo_gen4_info` ("Scarlett Gen 4"). Vendor obrigatório: **0x1235** (`VID %04x != expected 0x1235 for Focusrite`, string do GUI).
- O GUI descobre a placa por `/proc/asound/card%d/usbid` e `/proc/asound/card%d/usbbus`.
- Firmware: `/usr/lib/firmware/scarlett2/` (dir configurável por `SCARLETT2_FIRMWARE_DIR`), com checagem `Minimum Firmware Version` → `Firmware Update Required`.

Tabela completa do driver (para o nosso app suportar todos os modelos):
```
Solo 2i2 4i4 4th Gen : 1235:8218 1235:8219 1235:821a   (+ 16i16/18i16/18i20)
8i6 18i8 18i20 3rd   : 1235:82xx           Solo/2i2/4i4 3rd Gen
6i6 18i8 18i20 Gen 2 : 1235:82xx            Clarett USB 2Pre/4Pre/8Pre, Clarett+ 2Pre/4Pre/8Pre
Vocaster One/Two     : 1235:8217 1235:8216
```

## Estrutura da Solo Gen 4 (`solo_gen4_info`)
```
config_sets: firmware 2115 -> gen4_solo | firmware 2417 -> gen4_solo_2417
has_devmap = 1 (mapa de dispositivos baixável do aparelho)
level_input_count = 1  (Line/Inst)      air_input_count = 1 (Air = presence + drive, air_option=1)
phantom_count = 1 (48V)                 direct_monitor = 1
dsp_count = 2
portas: ANALOGUE {2,2}  PCM {1,0}  MIXER {6,2}  (n_in, n_out)
meter_map: 6 pares de medidor (ordem das barras de VU)
```

## Comandos USB do protocolo (SCARLETT2_USB_*)
`INIT_1`, `INIT_2`, `CMD_REQ`/`CMD_RESP` (controle), `SET_DATA`/`GET_DATA`, `SET_MIX`/`GET_MIX`,
`SET_MUX`/`GET_MUX`, `GET_METER` (+`METER_LEVELS_GET_MAGIC`), `GET_SYNC`, `GET_DEVMAP`,
`CONFIG_SAVE`, `REBOOT`, `INFO_FLASH`/`INFO_SEGMENT`/`INFO_DEVMAP`,
`READ_SEGMENT`/`WRITE_SEGMENT`/`ERASE_SEGMENT`, `GET_ERASE`, `DATA_CMD`.
Há também `fcp.h` (**Focusrite Control Protocol**) e a flag `SCARLETT2_USE_FCP_DRIVER` — o caminho novo
para Gen 4; o nosso app deve detectar e usar o mesmo que o kernel está usando.

## Interface do usuário (hwdep) — `scarlett2-uapi.h`
```c
SCARLETT2_IOCTL_PVERSION              // versão do protocolo
SCARLETT2_IOCTL_REBOOT
SCARLETT2_SEGMENT_ID_SETTINGS  = 0    // setor de flash das configurações
SCARLETT2_SEGMENT_ID_FIRMWARE  = 1
SCARLETT2_IOCTL_SELECT_FLASH_SEGMENT
SCARLETT2_IOCTL_ERASE_FLASH_SEGMENT
SCARLETT2_IOCTL_GET_ERASE_PROGRESS
```

## Vocabulário de controles (o que o app precisa desenhar)
`Master Playback`, `Master HW Playback`, `PCM <n> Capture Enum`, `Input Source <n> Capture Route`,
`Input Playback Route`, `Source Playback Enum`, `Direct Monitor Playback Enum`, `Level Capture Enum`,
`Level Meter`, `Gain Capture Volume`, `Autogain` / `Autogain Capture Switch` / `Autogain Status Capture Enum`,
`Gain Switch`, `input-mute`, `phantom`, `SPDIF`, `Impedance`, `Air`.

## Arquitetura do alsa-scarlett-gui (REA: 1044 procedimentos, 1008 strings, 212 KB)
- GTK4 + GResource embutido (extraído em `re/alsa-scarlett-gui/gresource/`: CSS + 5 SVG + 1 PNG).
- 4 janelas: `win.mixer`, `win.routing`, `win.levels`, `win.startup` (+ `win.about`, `win.load`, `win.save`).
- Grades de roteamento: `routing_{hw_in,pcm_in,pcm_out,hw_out,dsp_in,dsp_out,mixer_in,mixer_out}_grid`;
  preset `routing.preset('direct')`.
- Fala ALSA direto (`snd_ctl_elem_value_get/set_enumerated`, tipos `ENUMERATED`), medidores por `Level Meter`.
- Salva/carrega estado em arquivo `.state` do `alsactl`, e faz o **update de firmware** (grava no aparelho).

## Consequência para o SPOOK AUDIO
1. **Não** precisamos escrever driver: o painel do hardware = front-end GTK4 sobre **ALSA ctl** (`snd_ctl_elem_*`)
   e, para firmware/flash, sobre os **ioctls de hwdep**.
2. Suporte multi-modelo sai quase de graça: a tabela VID:PID → `device_info` do kernel é o mapa de features
   (níveis, air, phantom, direct monitor, portas, meter_map).
3. O `demo/*.state` do pacote é um catálogo pronto dos controles de cada modelo (útil para o painel).
