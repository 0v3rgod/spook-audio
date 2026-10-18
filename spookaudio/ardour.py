"""Ardour 9: motor (JACK/PipeWire · ALSA · PulseAudio · Dummy), sessão e transporte.

RE 03 — verificado no disco desta máquina:
  * binário real: /usr/lib/ardour9/ardour-9.8.0   (o /usr/bin/ardour9 é wrapper shell
    que exporta LD_LIBRARY_PATH=/usr/lib/ardour9 — por isso o `ldd` no binário cru
    acusa "not found" e mesmo assim o Ardour está saudável)
  * motores em /usr/lib/ardour9/backends/lib{jack,alsa,dummy,pulseaudio}_audiobackend.so
  * NÃO existe variável/flag de linha de comando para escolher o motor:
    `grep -c ARDOUR_BACKEND` no binário = 0 e o `--help` não tem flag de backend.
    A escolha é feita na janela "Audio/MIDI Setup" e fica gravada na sessão.
    Por isso o caminho confiável aqui é o JACK (que neste Arch é o pipewire-jack).
  * OSC: UDP 3819. Endereços confirmados em libs/surfaces/osc/osc.cc (upstream).
"""
from __future__ import annotations

import os
import re
import shutil
import signal
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import osc

BIN = "ardour9"
BIN_REAL = "/usr/lib/ardour9/ardour-9.8.0"
OSC_PORT = 3819
OSC_HOST = "127.0.0.1"
LOG_DIR = Path.home() / "spook-audio" / "logs"

# Backends que existem em /usr/lib/ardour9/backends/ (RE 03)
BACKENDS = ["jack", "alsa", "pulseaudio", "dummy"]
BACKEND_LABELS = {
    "jack": "JACK / PipeWire (recomendado)",
    "alsa": "ALSA direto (hw:X) — pede a janela do motor",
    "pulseaudio": "PulseAudio",
    "dummy": "Dummy (sem hardware — bom para testar)",
}
# O Ardour não aceita backend por linha de comando: isto explica o que o app faz.
BACKEND_NOTES = {
    "jack": "Sobe sob pw-jack: o Ardour encontra o JACK do PipeWire e conecta sozinho.",
    "alsa": "O Ardour não aceita ALSA por linha de comando. Abra 'Janela do motor' e escolha ALSA "
            "(fica salvo na sessão a partir daí).",
    "pulseaudio": "O Ardour não aceita PulseAudio por linha de comando. Use a Janela do motor.",
    "dummy": "Motor Dummy: a sessão abre sem placa nenhuma (útil para testar o painel).",
}

TRANSPORT = {
    "play":            ("/transport_play", ()),
    "stop":            ("/transport_stop", ()),
    "gravar":          ("/rec_enable_toggle", ()),
    "inicio":          ("/goto_start", ()),
    "fim":             ("/goto_end", ()),
    "loop":            ("/loop_toggle", ()),
    "marcador":        ("/add_marker", ()),
    "salvar":          ("/save_state", ()),
    "desfazer":        ("/undo", ()),
    "refazer":         ("/redo", ()),
    "metronomo":       ("/click/level", (0.5,)),
    "panico_midi":     ("/midi_panic", ()),
    "proxima_marca":   ("/next_marker", ()),
    "marca_anterior":  ("/prev_marker", ()),
    "punch":           ("/toggle_punch", ()),
    "rec_all":         ("/all_tracks_rec_in", ()),
}

FEEDBACK = {
    "/record_enabled": "gravando",
    "/is_recording": "gravando",
    "/transport_frame": "posicao",
    "/transport_speed": "velocidade",
    "/session_name": "sessao",
    "/group/list": "grupos (probe de vida)",
    "/reply": "resposta",
    "/position": "posicao",
}

ACOES_COMUNS = [
    "Transport/Record", "Transport/Play", "Transport/Stop", "Transport/ToggleRoll",
    "Editor/Save", "Editor/Undo", "Editor/Redo", "Editor/AddMarker",
    "Region/Crop", "Region/Split", "Track/NewAudioTrack", "Track/NewMidiTrack",
    "Window/ToggleMixer", "Window/ToggleEditor", "Common/ToggleMetronome",
]

LOG_DIR_DEFAULT = "logs"


def which() -> str | None:
    return shutil.which(BIN)


def installed() -> bool:
    return which() is not None or os.path.exists(BIN_REAL)


CONFIG = Path.home() / ".config" / "ardour9" / "config"

# como o Ardour chama cada motor (é o valor gravado em EngineHints/backend)
BACKEND_ARDOUR = {"jack": "JACK", "alsa": "ALSA", "pulseaudio": "PulseAudio", "dummy": "Dummy"}
BACKEND_LOCAL = {v.lower(): k for k, v in BACKEND_ARDOUR.items()}


def ler_config() -> dict:
    """Le ~/.config/ardour9/config (XML): opcoes e protocolos de controle.

    RE 03: o motor NAO fica aqui — quem guarda o motor e o proprio arquivo da
    sessao (tag <EngineHints>). Aqui ficam, por exemplo,
    default-session-parent-dir e o estado dos ControlProtocols (o OSC entre eles).
    """
    dados: dict = {"opcoes": {}, "protocolos": {}}
    if not CONFIG.exists():
        return dados
    try:
        import xml.etree.ElementTree as ET
        raiz = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return dados
    for opt in raiz.iter("Option"):
        nome = opt.get("name")
        if nome:
            dados["opcoes"][nome] = opt.get("value")
    for pr in raiz.iter("Protocol"):
        nome = pr.get("name")
        if nome:
            dados["protocolos"][nome] = pr.get("active") == "1"
    return dados


def default_session_dir() -> Path:
    """Pasta onde o Ardour cria sessoes (o wizard grava isso no config)."""
    pai = ler_config()["opcoes"].get("default-session-parent-dir")
    if pai and Path(pai).is_dir():
        return Path(pai)
    for cand in (Path.home() / "Documents" / "Ardour",
                 Path.home() / "Music" / "Ardour",
                 Path.home() / "Ardour"):
        if cand.is_dir():
            return cand
    return Path.home() / "Documents" / "Ardour"


def sessions() -> list[Path]:
    d = default_session_dir()
    if not d.is_dir():
        return []
    achadas = [p for p in d.iterdir() if p.is_dir() and any(p.glob("*.ardour"))]
    return sorted(achadas, key=lambda p: p.stat().st_mtime, reverse=True)


# --------------------------------------------------------------------- OSC
def osc_ativo() -> bool | None:
    """O Ardour só responde OSC se a surface 'Open Sound Control (OSC)' estiver ativa."""
    st = ler_config()["protocolos"]
    for nome, ativo in st.items():
        if "Open Sound Control" in nome:
            return ativo
    return None


def habilitar_osc(ativo: bool = True) -> tuple[bool, str]:
    """Liga/desliga o OSC no config do Ardour (com backup). Exige o Ardour fechado."""
    if not CONFIG.exists():
        return False, f"{CONFIG} nao existe (rode o Ardour uma vez)"
    try:
        texto = CONFIG.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return False, str(e)
    novo, n = re.subn(r'(<Protocol name="Open Sound Control \(OSC\)" )active="[01]"',
                      rf'\1active="{1 if ativo else 0}"', texto)
    if not n:
        return False, "nao achei o protocolo OSC no config"
    if texto == novo:
        return True, f"ja estava {'ligado' if ativo else 'desligado'}"
    bak = CONFIG.with_suffix(".config.spook-bak")
    if not bak.exists():
        try:
            bak.write_text(texto)
        except OSError:
            pass
    try:
        CONFIG.write_text(novo)
    except OSError as e:
        return False, f"nao consegui escrever: {e}"
    return True, f"OSC {'ligado' if ativo else 'desligado'} (backup: {bak.name})"


def osc_ativo_texto() -> str:
    st = ler_config()["protocolos"].get("Open Sound Control (OSC)")
    if st is None:
        return "config do Ardour ainda nao existe (abra o Ardour uma vez)"
    return "ligado" if st else "desligado (o transporte nao responde assim)"


# ------------------------------------------------------------ motor da sessao
def arquivo_sessao(sessao: Path) -> Path | None:
    if sessao.is_file() and sessao.suffix == ".ardour":
        return sessao
    if sessao.is_dir():
        arqs = sorted(sessao.glob("*.ardour"))
        if arqs:
            return arqs[0]
    return None


def engine_hints(sessao: Path) -> dict:
    """Le <EngineHints .../> da sessao: e AQUI que o motor escolhido fica."""
    arq = arquivo_sessao(sessao)
    if not arq:
        return {}
    try:
        txt = arq.read_text(errors="replace")
    except OSError:
        return {}
    m = re.search(r"<EngineHints([^>]*?)/?>", txt)
    if not m:
        return {}
    attrs = dict(re.findall(r'([A-Za-z_-]+)="([^"]*)"', m.group(1)))
    attrs["arquivo"] = arq
    return attrs


def definir_engine(sessao: Path, backend: str, entrada: str | None = None,
                   saida: str | None = None) -> tuple[bool, str]:
    """Troca o motor gravado na sessao (com o Ardour FECHADO).

    E o que substitui o dialogo 'Audio/MIDI Setup': o app grava
    <EngineHints backend="JACK" .../> e o Ardour sobe direto no motor escolhido.
    """
    arq = arquivo_sessao(sessao)
    if not arq:
        return False, "sessao nao encontrada"
    nome = BACKEND_ARDOUR.get(backend.lower(), backend)
    try:
        txt = arq.read_text(errors="replace")
    except OSError as e:
        return False, f"nao consegui ler: {e}"
    atual = engine_hints(arq)
    attrs = {"backend": nome}
    ent = entrada or atual.get("input-device")
    sai = saida or atual.get("output-device")
    if nome == "ALSA":
        if ent:
            attrs["input-device"] = ent
        if sai:
            attrs["output-device"] = sai
    novo_tag = "<EngineHints " + " ".join(f'{k}="{v}"' for k, v in attrs.items()) + "/>"
    novo, n = re.subn(r"<EngineHints[^>]*?/?>", novo_tag, txt)
    if not n:
        return False, "sessao sem <EngineHints> (abra-a uma vez no Ardour antes)"
    bak = arq.with_suffix(".ardour.spook-bak")
    if not bak.exists():
        try:
            bak.write_text(txt)
        except OSError:
            pass
    try:
        arq.write_text(novo)
    except OSError as e:
        return False, f"nao consegui escrever: {e}"
    return True, f"{arq.name}: motor -> {nome}"


def version() -> str | None:
    exe = which()
    if not exe:
        return None
    try:
        r = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30)
        out = (r.stdout or r.stderr).strip()
        return out.splitlines()[0] if out else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def build_command(session_name: str | None = None, new: bool = False,
                  backend: str = "jack", extra: list[str] | None = None) -> list[str]:
    exe = which() or BIN
    cmd = [exe, "--no-announcements", "--no-splash"]
    if session_name:
        if "/" in session_name:          # caminho de sessao existente
            cmd.append(session_name)
        elif new:
            cmd += ["-N", session_name]
        else:
            cmd.append(session_name)
    if extra:
        cmd += extra
    if backend == "jack" and shutil.which("pw-jack"):
        cmd = ["pw-jack", *cmd]
    return cmd


def preparar(sessao: Path, backend: str, ligar_osc: bool = True) -> list[str]:
    """Deixa tudo pronto antes de abrir uma sessao existente e conta o que fez."""
    avisos = []
    if ligar_osc:
        ok, msg = habilitar_osc(True)
        avisos.append(f"osc: {msg if ok else 'falhou — ' + msg}")
    ok, msg = definir_engine(Path(sessao), backend)
    avisos.append(f"motor: {msg if ok else 'nao alterado — ' + msg}")
    return avisos


def launch(session_name: str | None = None, new: bool = False, backend: str = "jack",
           disable_plugins: bool = False, no_connect_ports: bool = False,
           sessao: Path | None = None, aplicar_motor: bool = True,
           ligar_osc: bool = True) -> tuple[subprocess.Popen, Path]:
    """Sobe o Ardour em processo proprio (setsid) logando em ~/spook-audio/logs.

    Antes de abrir uma sessao existente: liga o OSC no config e grava o motor
    pedido no <EngineHints> da sessao (o Ardour nao aceita isso na linha de comando).
    """
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    campos = []
    if sessao and not new and aplicar_motor:
        campos = preparar(Path(sessao), backend, ligar_osc)
    elif ligar_osc:
        ok, msg = habilitar_osc(True)
        campos = [f"osc: {msg if ok else 'falhou — ' + msg}"]

    alvo = str(sessao) if sessao else session_name
    nome = Path(alvo).name if alvo else "novo"
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log = LOG_DIR / f"ardour-{nome}-{stamp}.log"
    extra = []
    if disable_plugins:
        extra.append("--disable-plugins")
    if no_connect_ports:
        extra.append("--no-connect-ports")
    cmd = build_command(alvo, new, backend, extra)

    env = dict(os.environ)
    env["ARDOUR_TRY_AUTOSTART_ENGINE"] = "1"
    fh = open(log, "wb")
    for linha in campos:
        fh.write(f"# spook: {linha}\n".encode())
    fh.flush()
    proc = subprocess.Popen(cmd, stdout=fh, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, env=env, start_new_session=True)
    return proc, log


def running() -> list[int]:
    """PIDs do Ardour (pgrep com [x] para nao casar com a propria shell)."""
    try:
        r = subprocess.run(["pgrep", "-f", r"[a]rdour-9\.[0-9]|[a]rdour9"],
                           capture_output=True, text=True, timeout=8)
    except (OSError, subprocess.TimeoutExpired):
        return []
    return [int(x) for x in r.stdout.split() if x.strip().isdigit()]


def is_running() -> bool:
    return bool(running())


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def stop(timeout: float = 6.0, force: bool = True) -> list[int]:
    """Encerra o Ardour: SIGTERM e, se insistir, SIGKILL. Devolve PIDs encerrados."""
    pids = running()
    if not pids:
        return []
    for p in pids:
        try:
            os.kill(p, signal.SIGTERM)
        except OSError:
            pass
    fim = time.time() + timeout
    while time.time() < fim and any(_alive(p) for p in pids):
        time.sleep(0.25)
    vivos = [p for p in pids if _alive(p)]
    if vivos and force:
        for p in vivos:
            try:
                os.kill(p, signal.SIGKILL)
            except OSError:
                pass
        time.sleep(0.5)
    return pids


@dataclass
class Transporte:
    """Transporte por OSC + leitura do feedback do Ardour."""

    host: str = "127.0.0.1"
    port: int = OSC_PORT
    feedback_port: int = 0
    client: object = None
    server: object = None
    ultimo: dict = None
    log: list = None
    ao_receber: object = None      # callback(opcional): (endereco, args)

    def __post_init__(self):
        # O Ardour responde para o endereco de ORIGEM da mensagem, entao o socket
        # que envia e o que recebe o feedback (um Server separado nunca recebia nada).
        self.client = osc.Client(self.host, self.port)
        self.client.bind_local()
        self.server = self.client.servidor(on_message=self._on_msg)
        self.ultimo = {}
        self.log = []

    def start(self):
        self.server.start()
        return self

    @property
    def porta_local(self) -> int:
        return self.client.local_port

    def _on_msg(self, addr, args, src):
        self.ultimo[addr] = args[0] if len(args) == 1 else list(args)
        self.log.append((addr, args))
        del self.log[:-250]
        if self.ao_receber:
            try:
                self.ao_receber(addr, args)
            except Exception:
                pass

    def send(self, acao: str) -> str:
        if acao in TRANSPORT:
            addr, args = TRANSPORT[acao]
        else:
            addr, args = (acao if acao.startswith("/") else f"/{acao}"), ()
        self.client.send(addr, *args)
        return addr

    def send_addr(self, addr: str, *args) -> None:
        self.client.send(addr, *args)

    def access_action(self, acao: str) -> None:
        self.client.send("/access_action", acao)

    def heartbeat(self, timeout: float = 1.5) -> bool:
        """Sonda de vida: o Ardour está escutando o OSC?

        CUIDADO: o OSC do Ardour NAO tem `/heartbeat` (isso e do TouchOSC). Quem
        ele responde SEMPRE e `/group/list`: `OSC::send_group_list()` manda de
        volta um `/group/list` para o endereco de origem da mensagem
        (libs/surfaces/osc/osc.cc). Se nada chegar, tentamos `/refresh`, que
        recria a surface e dispara o feedback global.
        """
        antes = len(self.log)
        self.client.send("/group/list")
        fim = time.time() + timeout
        while time.time() < fim:
            if len(self.log) > antes:
                return True
            time.sleep(0.05)
        self.client.send("/refresh")
        fim = time.time() + timeout / 2
        while time.time() < fim:
            if len(self.log) > antes:
                return True
            time.sleep(0.05)
        return False

    def resposta(self) -> str:
        """Ultima resposta recebida em texto (para o log da UI)."""
        if not self.log:
            return ""
        addr, args = self.log[-1]
        return f"{addr} {' '.join(map(str, args))}".strip()

    def close(self):
        try:
            self.server._stop.set()
        except Exception:
            pass
        self.client.close()


if __name__ == "__main__":
    print(f"instalado: {installed()} | versao: {version()}")
    print(f"rodando  : {is_running()} pids={running()}")
    cfg = ler_config()
    print(f"config   : {CONFIG} ({'existe' if CONFIG.exists() else 'nao existe'})")
    print(f"  pasta de sessoes : {default_session_dir()}")
    print(f"  OSC              : {osc_ativo_texto()}")
    print(f"  protocolos       : {sum(cfg['protocolos'].values())}/{len(cfg['protocolos'])} ligados")
    ss = sessions()
    print(f"sessoes  : {len(ss)}")
    for s in ss[:5]:
        h = engine_hints(s)
        print(f"  - {s.name:<24} motor={h.get('backend', '?'):<10} saida={h.get('output-device', '-')}")
    print(f"jack cmd : {' '.join(build_command('teste', new=True, backend='jack'))}")
    print(f"OSC      : {len(TRANSPORT)} atalhos + {len(ACOES_COMUNS)} acoes de /access_action")
