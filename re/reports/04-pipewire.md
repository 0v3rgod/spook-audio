# RE 04 — PipeWire 1.6.9 (o "sistema operacional de áudio" desta máquina)

`pipewire 1:1.6.9-1` · `pipewire-jack` · `pipewire-alsa` · `pipewire-pulse` · `wireplumber 0.5.18-1`.

## Por que ele é o núcleo da unificação
| API que o app fala | Quem dá | Efeito |
|---|---|---|
| **ALSA** | `pipewire-alsa` | apps `hw:`/`plughw` chegam no PipeWire |
| **JACK** | `pipewire-jack` | JACK nativo: `/usr/lib/libjack.so.0.3.1609` presente → apps JACK ligam **direto**, sem `pw-jack` (o `pw-jack` só ajusta `LD_LIBRARY_PATH`) |
| **PulseAudio** | `pipewire-pulse` | compatível com `pactl`/apps Pulse |
| **Grafo/roteamento** | `wireplumber` | políticas, sessões, dispositivos padrão |

Ou seja: **um servidor só** entrega as três APIs. Não há necessidade de "juntar" três
motores — a unificação é de **plano de controle** (nosso painel manda), não de código.

## Ferramentas de introspecção (todas presentes e usadas pelo painel)
| Ferramenta | O que devolve | Uso no app |
|---|---|---|
| `pw-dump` | JSON completo (objetos, nós, portas, links, props) | `spookaudio/pw.py::snapshot()` |
| `pw-link -l/-i/-o` | conexões existentes / entradas / saídas | contagem de links, patchbay |
| `wpctl status`/`get-volume` | dispositivos padrão, volume | linhas "saída/entrada padrão" |
| `pw-cli info <id>` | detalhe de um objeto | inspeção pontual |
| `jack_lsp` | portas JACK (via `pipewire-jack`) | linha "JACK (pipewire-jack)" da UI |
| `pw-jack <exe>` | roda um app JACK | lançamento do Ardour |

## Medições feitas aqui
```
grafo     : 11 nós (5 de áudio), 25 portas, 2 conexões
clock     : 48000 Hz · quantum 1024
jack_lsp  : responde (24 portas)  -> pipewire-jack ativo de verdade
cards ALSA: 0 NVidia HDA · 1 sof-hda-dsp      (a Scarlett está desplugada hoje)
```

## Duas camadas que NÃO se confundem
* **Fluxo de áudio** = PipeWire (nós/portas/links). É onde o som passa.
* **Controle de hardware** = `snd_ctl` do ALSA (mixer da placa). `spookaudio/alsa.py`
  fala isso por `ctypes` (`snd_ctl_open`, `snd_ctl_elem_info_all`, …) — sem dependência
  de `pyalsa`. É ortogonal ao PipeWire: o PipeWire roteia, o mixer da placa continua
  sendo do driver ALSA.
* Por isso o painel tem as duas abas separadas: **PATCHBAY** (PipeWire/JACK, roteamento)
  e **SCARLETT** (ALSA ctl, ganho/monitor/phantom da interface).

## Interface Focusrite Scarlett Solo 4th Gen
USB `1235:8218`, serial `T1EGXR0380613F`. **Desplugada hoje** — a detecção foi exercitada
(caminho testado) e devolve "nenhuma plugada" honestamente, em vez de inventar controles.
Com ela conectada, quem manda nos controles é o `alsa-scarlett-gui` (ver `01-scarlett-protocolo.md`).

## Riscos conhecidos
* Pegar `hw:` direto (Ardour em ALSA, por exemplo) **disputa a placa** com o PipeWire.
  Preferir JACK (pipewire-jack) ou Dummy quando não se quer tocar no que já está tocando.
* `pw-dump` é enorme; o filtro é por tipo (`PipeWire:Interface:Node` etc.) e por
  `media.class`, e o painel só lê o que precisa.
