"""Sessão modelo do Ardour — gerada a partir da sessão canônica desta máquina.

Por que existe (RE 03): o Ardour **não** aceita o motor por linha de comando; o motor
escolhido fica gravado dentro do arquivo da sessão (`<EngineHints backend="..."/>`) e o
OSC (`ControlProtocols`) também. Então "sessão modelo" = um `.ardour` já pronto com
motor + OSC gravados, para o painel abrir sem passar pelo diálogo *Audio/MIDI Setup*.

O esqueleto é uma cópia fiel da sessão `~/spook-teste` (salva pelo próprio Ardour 9.8,
version=7003) com os campos trocados por placeholders — em `spookaudio/data/`.
Nada aqui é inventado: abrir a sessão modelo é o mesmo que abrir `spook-teste`, só que
com outro nome, taxa, motor e OSC ligado.

NÃO cria trilhas: criar `Route` na mão é frágil e desnecessário — o painel cria trilhas
na sessão já aberta com OSC (`Track/NewAudioTrack`, ver `ardour.ACOES_COMUNS`).
"""
from __future__ import annotations

import re
import shutil
import uuid as _uuid
from pathlib import Path

from . import ardour

DATA = Path(__file__).with_name("data")
TEMPLATE = DATA / "sessao-modelo.ardour"

RATE_PADRAO = 48000
NOME_PADRAO = "spook-modelo"


class SessaoModeloError(RuntimeError):
    pass


# ------------------------------------------------------------------ helpers
def _xml_attr(texto: str, chave: str) -> str | None:
    m = re.search(rf'\b{re.escape(chave)}="([^"]*)"', texto)
    return m.group(1) if m else None


def ler(sessao: str | Path) -> dict:
    """Le os campos que importam de uma sessão (nome, taxa, motor, OSC, trilhas)."""
    arq = ardour.arquivo_sessao(Path(sessao))
    if not arq:
        return {}
    try:
        txt = arq.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    m_raiz = re.search(r"<Session\b[^>]*>", txt)
    raiz = m_raiz.group(0) if m_raiz else ""
    protocolos = dict(re.findall(r'<Protocol name="([^"]*)" active="([01])"', txt))
    rotas = re.findall(r'<Route\b[^>]*\bname="([^"]*)"[^>]*\bdefault-type="([^"]*)"', txt)
    return {
        "arquivo": arq,
        "pasta": arq.parent,
        "nome": _xml_attr(raiz, "name"),
        "uuid": _xml_attr(raiz, "uuid"),
        "versao": _xml_attr(raiz, "version"),
        "rate": _xml_attr(raiz, "sample-rate"),
        "backend": (ardour.engine_hints(arq) or {}).get("backend"),
        "osc": next((v == "1" for k, v in protocolos.items() if "Open Sound Control" in k), None),
        "trilhas": [n for n, tipo in rotas if n != "Master"],
        "master": any(n == "Master" for n, _ in rotas),
        "rotas": [n for n, _ in rotas],
    }


def template_ok() -> tuple[bool, str]:
    """Confere que o template está íntegro (placeholders presentes, XML de uma raiz)."""
    if not TEMPLATE.is_file():
        return False, f"template ausente: {TEMPLATE}"
    txt = TEMPLATE.read_text(encoding="utf-8")
    faltando = [p for p in ("@NOME@", "@UUID@", "@RATE@", "@BACKEND@", "@OSC@")
                if p not in txt]
    if faltando:
        return False, f"placeholders ausentes: {faltando}"
    if txt.count("<Session ") != 1:
        return False, "esperava exatamente um elemento <Session>"
    if "</Session>" not in txt:
        return False, "template sem </Session>"
    return True, f"{TEMPLATE.name}: {len(txt)} bytes, {len(faltando) + 5} placeholders ok"


def nome_valido(nome: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", nome or ""))


def destino_padrao(nome: str) -> Path:
    return ardour.default_session_dir() / nome


# ------------------------------------------------------------------ criação
def criar(nome: str = NOME_PADRAO, destino: Path | None = None, rate: int = RATE_PADRAO,
          backend: str = "jack", osc: bool = True, entrada: str = "", saida: str = "",
          force: bool = False) -> tuple[Path, list[str]]:
    """Cria a sessão modelo no disco. Devolve (pasta, avisos).

    Não abre o Ardour e não encosta em nada que esteja rodando.
    """
    if not nome_valido(nome):
        raise SessaoModeloError(f"nome inválido: {nome!r} (use letras, números, . _ -)")
    ok, msg = template_ok()
    if not ok:
        raise SessaoModeloError(f"template inválido — {msg}")

    pasta = Path(destino) if destino else destino_padrao(nome)
    arq = pasta / f"{nome}.ardour"
    if arq.exists() and not force:
        raise SessaoModeloError(f"já existe: {arq} (use force=True para sobrescrever)")

    txt = (TEMPLATE.read_text(encoding="utf-8")
           .replace("@NOME@", nome)
           .replace("@UUID@", str(_uuid.uuid4()))
           .replace("@RATE@", str(int(rate)))
           .replace("@BACKEND@", ardour.BACKEND_ARDOUR.get(backend.lower(), backend))
           .replace("@OSC@", "1" if osc else "0")
           .replace("@ENTRADA@", entrada or "")
           .replace("@SAIDA@", saida or ""))

    for sub in ("interchange", "peaks", "analysis", "export", "plugins", "externals", "dead"):
        (pasta / sub).mkdir(parents=True, exist_ok=True)
    arq.write_text(txt, encoding="utf-8")

    avisos = [f"sessão '{nome}' criada em {pasta}",
              f"motor gravado: {ardour.BACKEND_ARDOUR.get(backend.lower(), backend)}",
              f"OSC: {'ligado' if osc else 'desligado'}",
              f"taxa: {int(rate)} Hz"]
    if backend.lower() == "jack":
        avisos.append("para subir sob pipewire-jack: pw-jack ardour9 " + str(pasta))
    return pasta, avisos


def garantir(nome: str = NOME_PADRAO, **kw) -> tuple[Path, bool, list[str]]:
    """Devolve (pasta, criada_agora, avisos) — cria só se não existir."""
    pasta = Path(kw.pop("destino", None) or destino_padrao(nome))
    if ardour.arquivo_sessao(pasta):
        return pasta, False, [f"sessão já existe: {pasta}"]
    pasta, avisos = criar(nome, destino=pasta, **kw)
    return pasta, True, avisos


# ------------------------------------------------------------------ exemplo
def modelo_pronto() -> tuple[Path, list[str]]:
    """Sessão modelo do projeto: `spook-modelo`, motor JACK, OSC ligado, 48 kHz."""
    return garantir(NOME_PADRAO, rate=RATE_PADRAO, backend="jack", osc=True)[:2]


if __name__ == "__main__":
    ok, msg = template_ok()
    print(f"template : {'ok' if ok else 'PROBLEMA'} — {msg}")
    ss = ardour.sessions()
    print(f"sessões  : {len(ss)} em {ardour.default_session_dir()}")
    for s in ss[:5]:
        d = ler(s)
        print(f"  - {(d.get('nome') or '?'):<20} motor={(d.get('backend') or '?'):<8} "
              f"osc={d.get('osc')} rate={d.get('rate')} trilhas={len(d.get('trilhas', []))}")
    print(f"faltando : {[k for k in ('jack', 'dummy') if k not in ardour.BACKENDS]}")
