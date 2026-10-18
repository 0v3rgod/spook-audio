# RE 03 — Ardour 9.8 (`ardour 9.8-1`)

Tudo abaixo foi lido **no disco desta máquina** (binário, `.so`, fontes upstream do
`libs/ardour` e `libs/surfaces/osc`) e conferido rodando.

## Anatomia
| Peça | Caminho | Observação |
|---|---|---|
| Lançador | `/usr/bin/ardour9` | é um **script `sh`**: exporta `GTK2_RC_FILES=/dev/null`, `GTK_PATH=/etc/ardour9:/usr/lib/ardour9`, `LD_LIBRARY_PATH=/usr/lib/ardour9` e faz o `exec` |
| Binário real | `/usr/lib/ardour9/ardour-9.8.0` | por isso `ldd` no binário cru acusa "not found" e o Ardour está saudável |
| Motores (plugins) | `/usr/lib/ardour9/backends/lib{jack,alsa,dummy,pulseaudio}_audiobackend.so` | JACK 219 KB · ALSA 342 KB · Dummy 170 KB · PulseAudio 137 KB |
| Config global | `~/.config/ardour9/config` | XML: `<Config><Option name=.../>`, `<Metadata/>`, `<ControlProtocols><Protocol name=... active="0\|1"/>` |
| Estado da sessão | `~/.config/ardour9/instant.xml`, `session_port.rc` | `instant.xml` = editor; `session_port.rc` = conexões externas por motor, ex. `<ExtConnection for="ALSA;;HDA NVidia">` |
| Sessões | pasta de `default-session-parent-dir` | na 1ª execução o assistente gravou `default-session-parent-dir=/home/taylor` |

Versão: `Ardour9.8.0 (built using 9.8 and GCC version 16.2.1 20260810)`.

## ⚠️ Não existe variável/flags para escolher o motor
* `strings /usr/lib/ardour9/ardour-9.8.0 | grep -c ARDOUR_BACKEND` → **0**.
* `--help` traz `-N/--new`, `-n/--no-splash`, `-d/--disable-plugins`,
  `-P/--no-connect-ports`, `-T/--template`, `-A/--actions`, `-c/--name` — **nenhuma de motor**.
* A escolha é feita na janela **Audio/MIDI Setup** (`set_backend()` em
  `engine_dialog.cc:1033` quando você clica, `:2216` quando restaura `state->backend`).
* `AudioEngine::setup_required()` (`libs/ardour/audioengine.cc:1532-1547`) devolve
  `false` **só** quando o backend escolhido já está `already_configured()`. Com 4 motores
  instalados, o diálogo é obrigatório na 1ª vez.
* `Config->get_try_autostart_engine()` (`engine_dialog.cc:480`, o botão *Autostart*)
  liga/desliga a tentativa de subir o motor sem diálogo — é o `ARDOUR_TRY_AUTOSTART_ENGINE`.

## O motor fica gravado DENTRO DA SESSÃO (achado principal)
Sessão criada no teste (`/home/taylor/spook-teste/spook-teste.ardour`):

```xml
<EngineHints backend="ALSA" input-device="sof-hda-dsp" output-device="HDA NVidia"/>
```

* **Não há chave de motor no `config`** — o XML da sessão é a fonte da verdade.
* Consequência prática: dá para **trocar o motor de uma sessão existente** escrevendo
  essa tag (com o Ardour fechado) e subir sem passar por diálogo nenhum. É assim que
  `spookaudio/ardour.py::definir_engine()` funciona (faz backup `*.ardour.spook-bak`).
* `session_port.rc` guarda as conexões do backend ALSA por dispositivo
  (`ExtConnection for="ALSA;;HDA NVidia"`), e é regravado junto.

## OSC (controle remoto)
* É um **ControlProtocol**, e vem **`active="0"` por padrão** → o transporte não
  responde *nada* até você ligar (Preferences → Control Surfaces → OSC, ou
  `active="1"` no `config`, com o Ardour fechado: é o que
  `spookaudio/ardour.py::habilitar_osc()` faz, com backup `.config.spook-bak`).
* Porta **UDP 3819**. Os endereços **não** têm prefixo `/ardour` (o macro `X_()` é
  identidade): `/transport_play`, `/transport_stop`, `/record_enabled`,
  `/session_name`, `/access_action <Ação>`, `/group/list`, `/strip/list`, `/refresh`,
  `/set_surface`, `/surface/list`, `/loop_toggle`, `/add_marker`.
* **`/heartbeat` NÃO EXISTE no Ardour** (isso é do TouchOSC). Quem **sempre** responde é
  `/group/list`: `OSC::send_group_list()` (`osc.cc:3390`) devolve
  `/group/list "none" ...` para o **endereço de origem** da mensagem. É a sonda de vida
  usada pelo painel (`ardour.py::Transporte.heartbeat()`), com `/refresh` como reserva.
* Como o feedback vai para o **endereço de origem**, o app precisa **enviar e receber no
  mesmo socket** (`osc.Client.servidor()`); um servidor ouvindo em outra porta nunca
  recebe nada.

## Fluxo de primeira execução (observado sob Xvfb + OCR)
1. Assistente: *Bem-vindo ao Ardour* → escala de GUI/fonte → *Ardour is ready for use*
   (Cancelar / Voltar / **Aplicar** — o OCR lê "Apllcar").
2. Diálogo **Configurar Sessão** (modelo + nome; a fileira de botões tem baixo contraste
   e o OCR perde — clicar no canto inferior direito).
3. Só então a janela do editor. Nada disso é pulável por linha de comando: o `-N`
   cria a sessão, mas o diálogo de motor continua necessário na 1ª vez.

**Limitação do ambiente de teste (não do Ardour):** sob Xvfb **sem window manager** o
Ardour para em `Detecting Audio/MIDI Devices` e a janela não aparece, então o E2E do
motor não pôde ser fechado aqui. No Hyprland real não há esse bloqueio.

## Caminho adotado pelo painel
```
pw-jack /usr/bin/ardour9 <sessão>          # JACK = pipewire-jack (nativo aqui)
ARDOUR_TRY_AUTOSTART_ENGINE=1              # evita o diálogo quando já há hints
+ habilitar_osc()                          # senão o transporte não fala
+ definir_engine(sessão, "jack"|"alsa"|"dummy"|"pulseaudio")
```
* **JACK/PipeWire** é o caminho confiável: `pipewire-jack` já fornece `libjack.so.0`;
  o `pw-jack` é opcional (só ajusta as libs por `LD_LIBRARY_PATH`).
* **ALSA** funciona (foi o motor que o assistente escolheu: `HDA NVidia`), mas toma o
  dispositivo de hardware — com o PipeWire rodando isso é disputa pela placa.
* **Dummy** é útil de verdade: abre a sessão **sem placa nenhuma** (teste de painel/UI).
* **PulseAudio** existe como plugin e, neste Arch, o "Pulse" é o `pipewire-pulse`.
