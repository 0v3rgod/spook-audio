"""Carla: host de plugins (LV2/VST2/VST3/CLAP/LADSPA/DSSI/SF2/SFZ/GIG).

RE 02 — do que foi verificado:
  * carla-git 2.6.0-alpha1, Qt 5.15.18, 92 arquivos Python em /usr/share/carla/
  * modos: carla-rack / carla-patchbay / carla-jack-multi / carla-jack-single /
    carla-control / carla-settings
  * libs: libcarla_standalone2.so (engine), libcarla_host-plugin.so (VST3/LV2/VST2),
    carla-bridge-lv2.so, interposers (carla-bridge-*)
  * integração: OSC em TCP e UDP na porta 22752 (carla_shared.py / carla_host_control.py)
    — o carla-control é um cliente OSC do carla-rack, e é por aí que o painel fala com ele.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

from . import osc

OSC_PORT = 22752           # CARLA_OSC_PORT default (RE 02)
MODOS = {
    "rack":      ("carla-rack",      "Rack — plugins em série/cadeia"),
    "patchbay":  ("carla-patchbay",  "Patchbay — roteamento livre"),
    "jack-multi": ("carla-jack-multi", "JACK Multi-Client"),
    "jack-single": ("carla-jack-single", "JACK Single-Client"),
    "control":   ("carla-control",   "Control — controle remoto (OSC)"),
    "settings":  ("carla-settings",  "Settings"),
}
LOG_DIR = Path.home() / "spook-audio" / "logs"
PROJETOS_DIR = Path.home() / "spook-audio" / "carla"


def binario(modo: str) -> str | None:
    return shutil.which(MODOS.get(modo, (modo,))[0])


def instalado() -> bool:
    return which() is not None


def which() -> str | None:
    return shutil.which("carla-rack")


def versao() -> str | None:
    if not shutil.which("carla"):
        return None
    try:
        r = subprocess.run(["carla", "--version"], capture_output=True, text=True, timeout=25)
        out = (r.stdout or r.stderr).strip()
        return out.splitlines()[0] if out else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def projetos() -> list[Path]:
    """Projetos .carxp encontrados nos diretórios prováveis."""
    achados: list[Path] = []
    for base in (Path.home() / "spook-audio" / "carla", Path.home() / "Documents", Path.home()):
        if not base.is_dir():
            continue
        for p in sorted(base.glob("*.carxp"), key=lambda x: x.stat().st_mtime, reverse=True):
            if p.is_file() and p not in achados:
                achados.append(p)
    return achados[:30]


def running(modo: str | None = None) -> list[int]:
    padrao = r"[c]arla-rack|[c]arla-patchbay|[c]arla-jack"
    if modo and modo in MODOS:
        padrao = "[c]arla-" + modo
    try:
        r = subprocess.run(["pgrep", "-f", padrao], capture_output=True, text=True, timeout=8)
    except (OSError, subprocess.TimeoutExpired):
        return []
    return [int(x) for x in r.stdout.split() if x.strip().isdigit()]


def comando(modo: str = "rack", projeto: str | Path | None = None,
            usar_jack: bool = True, extra: list[str] | None = None) -> list[str]:
    """Monta a linha de comando do Carla (sem executar)."""
    if modo not in MODOS:
        raise ValueError(f"modo desconhecido: {modo} (use um de {list(MODOS)})")
    exe = shutil.which(MODOS[modo][0])
    if not exe:
        raise FileNotFoundError(f"{MODOS[modo][0]} nao instalado")
    cmd = [exe]
    if projeto:
        cmd.append(str(projeto))
    if extra:
        cmd += extra
    if usar_jack and modo in ("rack", "patchbay", "jack-multi", "jack-single") and shutil.which("pw-jack"):
        cmd = ["pw-jack", *cmd]
    return cmd


def launch(modo: str = "rack", projeto: str | Path | None = None,
           usar_jack: bool = True, extra: list[str] | None = None) -> tuple[subprocess.Popen, Path]:
    """Abre o Carla no modo pedido (padrão: rack), opcionalmente com um projeto .carxp."""
    cmd = comando(modo, projeto, usar_jack, extra)
    log_dir = Path.home() / "spook-audio" / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log = log_dir / f"carla-{modo}-{time.strftime('%Y%m%d-%H%M%S')}.log"

    fh = open(log, "wb")
    proc = subprocess.Popen(cmd, stdout=fh, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, start_new_session=True)
    return proc, log


def client(host: str = "127.0.0.1", port: int = OSC_PORT) -> osc.Client:
    """Cliente OSC para o carla-rack (o carla-control usa o mesmo caminho)."""
    return osc.Client(host, port)


def enviar(texto: str, host: str = "127.0.0.1", port: int = OSC_PORT) -> bool:
    """Envia um comando OSC cru ao Carla (ex: '/carla/set_active true')."""
    c = client(host, port)
    try:
        endereco = texto.split(" ", 1)[0]
        c.send(endereco)
        return True
    except OSError:
        return False
    finally:
        c.close()


if __name__ == "__main__":
    for m, (exe, desc) in MODOS.items():
        print(f"  {m:<12} {exe:<20} {'OK' if shutil.which(exe) else 'ausente':<8} {desc}")
    print(f"projetos .carxp: {len(projetos())}")
    for p in projetos()[:5]:
        print(f"  - {p}")
    print(f"porta OSC: {OSC_PORT}")
    print(f"rodando: {running()}")
