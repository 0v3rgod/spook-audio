# Fase 4 — PoC: um binário nosso dirigindo o `libardour` instalado

**Status: FUNCIONANDO (14/10/2026).** `fase4/engine/spook-audio-engine` linka o
`libardour.so.3.0.0` do pacote `ardour` (9.8) **sem nenhum header de GUI**, sobe o
motor, cria/abre uma sessão, cria trilha, lê as rotas e encerra limpo.

```
$ ./run.sh --backend "None (Dummy)" --session /tmp/opencode/spook-sess2/pocsessao PoC2 \
           --track MinhaTrilha --seconds 2
ARDOUR::init(): ok
motor no ar: Dummy | sample_rate=48000 Hz
abrindo/criando sessao em /tmp/opencode/spook-sess2/pocsessao (snapshot "PoC2")
sessao: "PoC2" sample_rate=48000
criando trilha "MinhaTrilha"...
  criadas: 1
rotas na sessao: 1
  - MinhaTrilha
rodando o motor por 2s...
encerrado limpo.
```

Isso prova o que a Fase 4 precisava provar: **dá para usar o motor do Ardour como
biblioteca dentro de um processo nosso, com a nossa UI** — não é preciso abrir o
`ardour9`, nem falar OSC com ele.

---

## Por que isto é possível (o que ficou provado)

| achado | onde |
|---|---|
| `libardour.so.3.0.0` (15,9 MB, ~11 000 símbolos `ARDOUR::`) **não tem toolkit de GUI** (`ldd` sem gtk/ytk/ydk) | `re/reports/03-ardour.md` |
| a GUI está separada em `libwidgets.so` + `ardour-9.8.0` | idem |
| os backends são **plugins carregáveis**: `backends/lib{alsa,dummy,jack}_audiobackend.so` + `libpulseaudio_backend.so` | idem |
| já existe um Ardour headless instalado: `/usr/lib/ardour9/hardour-9.8.0` | idem |
| `ARDOUR::Session` é acessível por API pública (`new_audio_track`, `get_routes`, …) | este PoC |

O bootstrap saiu do próprio Ardour: `headless/load_session.cc`. Os stubs
`vstfx_init/vstfx_exit/vstfx_destroy_editor` são obrigatórios — o `libardour` os
deixa **indefinidos de propósito** (quem os fornece é o host VST).

---

## O bug que custou o dia: 16 bytes de ABI

Primeiro sintoma: `--session` criava a sessão e a trilha, e então **SIGSEGV** dentro de
`ARDOUR::Session::get_routes()` (`RCUManager::reader()`, precedido de
`pthread_mutex_lock: assertion mutex->__data.__owner == 0`).

**Causa raiz:** o build do Ardour compila com **`-DWAF_BUILD`**
(`ardour/wscript:1331-1332`). Só com esse define os headers incluem os `*-config.h`
gerados:

```c
/* libs/ardour/ardour/session.h:34 */
#ifdef WAF_BUILD
#include "libardour-config.h"
#endif
```

Sem `-DWAF_BUILD`, o `libardour-config.h` (que nós mesmos geramos à mão, com
`USE_TLSF`) **nunca era incluído** → `USE_TLSF` indefinido → este membro mudava de tipo:

```c
/* session.h:1690 */
#ifdef USE_TLSF
	PBD::TLSF        _mempool;   /* 40 B: std::string(32) + char*(8)            */
#else
	PBD::ReallocPool _mempool;   /* 56 B: std::string(32) + size_t(8) + 2 char* */
#endif
```

`ReallocPool` − `TLSF` = **+16 bytes**, e daí **todo o resto da classe desliza 16 B**.
`sizeof(ARDOUR::Session)` ficava 15552 em vez de 15536, e o `routes`
(`SerializedRCUManager`, logo depois de `_route_groups`) caía em `+0x30e8` em vez de
`+0x30d8`. Como `get_routes()` é **inline no header**, cada chamada nossa lia o membro
16 bytes ao lado → lixo → SIGSEGV.

**Fix:** uma flag. `build.sh` agora compila com `-DWAF_BUILD`.

Verificação: `sizeof(ARDOUR::Session)` = 15536 (bate com a lib) e **os 97 offsets de
membros top-level batem 100%** — ver `tools/abi-check.sh`.

> **Regra para o projeto:** qualquer atualização do pacote `ardour` pode mudar o
> layout. Rode `fase4/tools/abi-check.sh` depois de atualizar, **antes** de culpar o
> resto do código.

---

## Como diagnosticar isto de novo (as ferramentas)

### 1. O layout do `libardour` INSTALADO (autoritativo)

O `.so` vem *stripped* mas com `.gnu_debuglink`; o debug está no debuginfod do Arch
(build-id `8ce70df22aca1ef0b4496ef8dc9098569f91aed0`, ~255 MB, cacheado em
`~/.cache/debuginfod_client`):

```bash
DEBUGINFOD_URLS=https://debuginfod.archlinux.org \
  gdb -batch -q -iex "set debuginfod enabled on" \
      -ex "ptype /o ARDOUR::Session" /usr/lib/ardour9/libardour.so.3.0.0
```

O `-iex` (antes de carregar o arquivo) é obrigatório: em batch, o gdb **recusa** o
auto-download se a pergunta chegar primeiro. No fim do dump vem
`/* total size (bytes): 15536 */`.

### 2. O layout do NOSSO build

- `g++ -fdump-lang-class` dá o **tamanho** e os offsets das bases — **não** os dos
  membros. (`gcc` nunca imprime membros nesse dump.)
- Para os **offsets dos membros** há duas armadilhas:
  - o `gcc` **não** emite a definição completa da classe no DWARF quando o TU só usa
    funções inline (nem com `-fno-eliminate-unused-debug-types`; só saem DIEs com
    `DW_AT_declaration`). `gdb ptype`/`pahole` num `.o` nosso falham por isso.
  - as seções `.debug_*` do `gcc` nesta máquina saem **comprimidas**
    (`SHF_COMPRESSED`) → `strings`/`grep -a` **não** acham nenhum nome. Use `gdb` ou
    `readelf --debug-dump=info`.
- O que funciona: `sed` num **cópia** do `session.h` trocando `private:`/`protected:`
  por `public:` e gerar `offsetof(ARDOUR::Session, …)` para os nomes extraídos do
  `ptype` do passo 1.
  **Não** usar `-Dprivate=public` global: quebra a libstdc++ (`sstream`:
  `__xfer_bufptrs redeclared with different access`). Escopo tem de ser só o
  `session.h`.

Tudo isso está automatizado em `tools/abi-check.sh`.

### 3. Outra pista barata: o tamanho

`objdump -d` do construtor **não** ajuda (a alocação é do chamador). Mas o
`gdb ptype /o` do passo 1 já imprime `total size`, e `-fdump-lang-class` do nosso lado
também: se os dois números diferirem, você tem divergência de layout — e o delta
(16 B) já aponta para uma troca de tipo por macro de config.

---

## Armadilhas já mapeadas do `libardour`

- **Criar sessão:** passe um diretório **que não existe**. `_is_new = !Glib::file_test(_path, IS_DIR)`
  (`session_state.cc:178`). Se você já criar o diretório, o Ardour tenta *abrir* uma
  sessão vazia.
- **Backend "None (Dummy)"** não aparece em `available_backends()` — é filtrado por
  `Config->get_hide_dummy_backend()` (`audioengine.cc:961-975`). Use `set_backend()`
  direto. Nós dependemos disso para testar sem tocar em som.
- **Destruição:** `engine->remove_session(); delete session; engine->stop(); ARDOUR::cleanup();`
- **`Receiver`/`Transmitter`** estão no namespace **global** (não em `PBD::`).
- **`LD_LIBRARY_PATH=/usr/lib/ardour9`** é obrigatório: o RUNPATH não cobre as
  dependências transitivas (`libsuil.so.0` etc.). `run.sh` cuida disso, junto com
  `ARDOUR_DATA_PATH`/`ARDOUR_CONFIG_PATH`/`ARDOUR_DLL_PATH`/`VAMP_PATH`.

## Arquivos

| arquivo | papel |
|---|---|
| `engine/spook-audio-engine.cc` | o programa: modos `--probe`, `--backends`, `--backend`, `--session`, `--track`, `--seconds` |
| `build.sh` | compila+linka (`-DWAF_BUILD`, `-DNDEBUG`, includes de todas as `libs/*`) |
| `run.sh` | exporta o ambiente que o wrapper `/usr/bin/ardour9` monta |
| `build/libardour-config.h`, `build/libpbd-config.h` | os `*-config.h` gerados **à mão** (o waf não roda aqui) |
| `tools/abi-check.sh` | confere o nosso layout contra o da lib instalada |
| `src/ardour/` | fonte 9.8 (`git clone --depth 1 --branch 9.8`) — só para os headers |
