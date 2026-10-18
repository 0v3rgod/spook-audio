"""PipeWire: leitura do grafo (pw-dump) e manipulacao de conexoes (pw-link).

O PipeWire e o nucleo do SPOOK AUDIO: ele ja entrega ALSA (pipewire-alsa),
JACK (pipewire-jack) e Pulse (pipewire-pulse), entao nao existe nada para
"portar" — o painel so orquestra (RE 04).
"""
from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass, field

TIMEOUT = 12


def _run(cmd: list[str], timeout: int = TIMEOUT) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def has(binary: str) -> bool:
    return shutil.which(binary) is not None


@dataclass
class Node:
    id: int
    name: str
    desc: str
    media_class: str
    client_id: int
    state: str
    n_in: int
    n_out: int
    props: dict = field(default_factory=dict)

    @property
    def is_sink(self) -> bool:
        return self.media_class in ("Audio/Sink", "Audio/Duplex")

    @property
    def is_source(self) -> bool:
        return self.media_class in ("Audio/Source", "Audio/Duplex")

    @property
    def is_app(self) -> bool:
        return self.media_class.startswith("Stream/")

    @property
    def is_jack(self) -> bool:
        return "jack" in (self.props.get("client.api") or self.props.get("node.group") or "").lower()


@dataclass
class Port:
    id: int
    node_id: int
    name: str
    direction: str        # 'in' | 'out'
    channel: str


@dataclass
class Link:
    id: int
    out_node: int
    out_port: int
    in_node: int
    in_port: int
    state: str


@dataclass
class Snapshot:
    nodes: dict[int, Node] = field(default_factory=dict)
    ports: dict[int, Port] = field(default_factory=dict)
    links: list[Link] = field(default_factory=list)
    devices: dict[int, dict] = field(default_factory=dict)
    metadata: list[dict] = field(default_factory=list)

    def port_of(self, node_id: int, direction: str) -> list[Port]:
        return [p for p in self.ports.values() if p.node_id == node_id and p.direction == direction]

    def audio_nodes(self) -> list[Node]:
        return sorted((n for n in self.nodes.values() if n.media_class.startswith("Audio")),
                      key=lambda n: (n.is_app, n.desc.lower()))

    def links_between(self, out_node: int, in_node: int) -> list[Link]:
        return [l for l in self.links if l.out_node == out_node and l.in_node == in_node]

    def achar(self, padrao: str) -> list[Node]:
        """Nós cujo `name` ou `desc` casa com a regex (ex: r'carla|ardour')."""
        import re
        rx = re.compile(padrao, re.I)
        return sorted((n for n in self.nodes.values()
                       if rx.search(n.name or "") or rx.search(n.desc or "")),
                      key=lambda n: n.name or "")

    def portas_de(self, node_id: int) -> tuple[list[Port], list[Port]]:
        """(saídas, entradas) de um nó, ordenadas por nome (para parear por canal)."""
        outs = sorted(self.port_of(node_id, "out"), key=lambda p: p.name)
        ins = sorted(self.port_of(node_id, "in"), key=lambda p: p.name)
        return outs, ins


def dump() -> list[dict]:
    if not has("pw-dump"):
        return []
    try:
        out = _run(["pw-dump"]).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    try:
        return json.loads(out) if out.strip() else []
    except json.JSONDecodeError:
        return []


def snapshot(objs: list[dict] | None = None) -> Snapshot:
    objs = dump() if objs is None else objs
    snap = Snapshot()
    for o in objs:
        t = o.get("type", "")
        info = o.get("info") or {}
        props = info.get("props") or {}
        oid = o.get("id")
        if t == "PipeWire:Interface:Node":
            snap.nodes[oid] = Node(
                id=oid,
                name=props.get("node.name", "?"),
                desc=props.get("node.description") or props.get("node.nick") or props.get("node.name", "?"),
                media_class=props.get("media.class", ""),
                client_id=int(info.get("client-id", 0) or 0),
                state=info.get("state", ""),
                n_in=int(info.get("n-input-ports", 0) or 0),
                n_out=int(info.get("n-output-ports", 0) or 0),
                props=props,
            )
        elif t == "PipeWire:Interface:Port":
            snap.ports[oid] = Port(
                id=oid,
                node_id=int(props.get("node.id", 0) or 0),
                name=props.get("port.name", "?"),
                direction=props.get("port.direction", "?"),
                channel=props.get("audio.channel", ""),
            )
        elif t == "PipeWire:Interface:Link":
            snap.links.append(Link(
                id=oid,
                out_node=int(info.get("output-node-id", 0) or 0),
                out_port=int(info.get("output-port-id", 0) or 0),
                in_node=int(info.get("input-node-id", 0) or 0),
                in_port=int(info.get("input-port-id", 0) or 0),
                state=info.get("state", ""),
            ))
        elif t == "PipeWire:Interface:Device":
            snap.devices[oid] = {
                "name": props.get("device.name", "?"),
                "desc": props.get("device.description") or props.get("device.nick") or "?",
                "api": props.get("device.api", "?"),
                "vendor": props.get("device.vendor.id", ""),
                "product": props.get("device.product.id", ""),
                "bus": props.get("device.bus", ""),
            }
        elif t == "PipeWire:Interface:Metadata":
            for entry in info.get("metadata") or []:
                snap.metadata.append({"key": entry.get("key"), "value": entry.get("value")})
    return snap


def _md_value(snap: Snapshot, key: str):
    val = None
    for m in snap.metadata:
        if m.get("key") == key:
            val = m.get("value")     # o ultimo vence
    if isinstance(val, dict):
        return val.get("name") or val.get("id")
    return val


def default_sink(snap: Snapshot | None = None) -> str | None:
    snap = snap or snapshot()
    name = _md_value(snap, "default.audio.sink")
    if isinstance(name, str) and name in {n.name for n in snap.nodes.values()}:
        return name
    # WirePlumber guarda o default no metadado proprio dele; wpctl resolve sempre.
    if has("wpctl"):
        try:
            out = _run(["wpctl", "inspect", "@DEFAULT_AUDIO_SINK@"], timeout=6).stdout or ""
            for line in out.splitlines():
                line = line.strip()
                if line.startswith("id ") and line.split(",")[0][3:].isdigit():
                    nid = int(line.split(",")[0][3:])
                    if nid in snap.nodes:
                        return snap.nodes[nid].name
        except (OSError, subprocess.TimeoutExpired):
            pass
    return None


def default_source(snap: Snapshot | None = None) -> str | None:
    snap = snap or snapshot()
    return _md_value(snap, "default.audio.source")


def services() -> dict[str, str]:
    """Estado dos servicos do usuario que sustentam o audio."""
    nomes = ["pipewire", "pipewire-pulse", "wireplumber", "filter-chain"]
    estados: dict[str, str] = {}
    for n in nomes:
        try:
            r = _run(["systemctl", "--user", "is-active", n], timeout=6)
            estados[n] = (r.stdout or r.stderr).strip() or "unknown"
        except (OSError, subprocess.TimeoutExpired):
            estados[n] = "unknown"
    return estados


def jack_status() -> dict:
    """O JACK que existe aqui e o pipewire-jack. Testa se responde."""
    if not has("jack_lsp"):
        return {"disponivel": False, "portas": 0, "detalhe": "jack_lsp nao instalado"}
    try:
        r = _run(["jack_lsp"], timeout=6)
    except (OSError, subprocess.TimeoutExpired):
        return {"disponivel": False, "portas": 0, "detalhe": "timeout"}
    portas = [p for p in (r.stdout or "").splitlines() if p.strip()]
    return {"disponivel": r.returncode == 0, "portas": len(portas),
            "detalhe": "pipewire-jack" if r.returncode == 0 else (r.stderr or "").strip()[:120]}


def clock(snap: Snapshot | None = None) -> dict:
    """Taxa/quantum atuais (via pw-metadata ou props do driver)."""
    info: dict[str, object] = {}
    if has("pw-metadata"):
        try:
            out = _run(["pw-metadata", "-n", "settings"], timeout=6).stdout or ""
            for line in out.splitlines():
                if "clock.rate" in line or "clock.quantum" in line:
                    chave = "clock.rate" if "clock.rate" in line else "clock.quantum"
                    for token in line.replace("'", " ").replace('"', " ").split():
                        if token.isdigit():
                            info[chave] = int(token)
                            break
        except (OSError, subprocess.TimeoutExpired):
            pass
    snap = snap or snapshot()
    for n in snap.nodes.values():
        if n.props.get("media.class") == "Audio/Sink" or n.props.get("node.name") == "alsa_output":
            if "audio.rate" in n.props:
                info.setdefault("clock.rate", int(n.props["audio.rate"]))
    return info


def link(out: str, inp: str) -> tuple[bool, str]:
    try:
        r = _run(["pw-link", out, inp], timeout=8)
        return r.returncode == 0, (r.stderr or r.stdout).strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, str(e)


def unlink(out: str, inp: str) -> tuple[bool, str]:
    try:
        r = _run(["pw-link", "-d", out, inp], timeout=8)
        return r.returncode == 0, (r.stderr or r.stdout).strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, str(e)


def port_path(node_name: str, port_name: str) -> str:
    return f"{node_name}:{port_name}"


# ------------------------------------------------------------------ patch (Fase 2)
def plano_par(snap: Snapshot, origem_padrao: str, destino_padrao: str,
              canal_extra: bool = True) -> tuple[list[tuple[str, str]], str]:
    """Plano de conexão entre dois nós achados por regex (pareando por canal).

    Devolve ([(saida, entrada)], motivo). Se a origem tiver menos saídas que as
    entradas do destino, a última saída é repetida (mono → estéreo), o que é
    exatamente o caso da Scarlett Solo (1 pré → 2 canais).
    """
    ors = snap.achar(origem_padrao)
    des = snap.achar(destino_padrao)
    if not ors:
        return [], f"origem '{origem_padrao}' nao encontrada no grafo"
    if not des:
        return [], f"destino '{destino_padrao}' nao encontrado no grafo"
    o, d = ors[0], des[0]
    outs, _ = snap.portas_de(o.id)
    _, ins = snap.portas_de(d.id)
    if not outs:
        return [], f"'{o.name}' nao tem porta de saida"
    if not ins:
        return [], f"'{d.name}' nao tem porta de entrada"
    plano: list[tuple[str, str]] = []
    for i, entrada in enumerate(ins):
        if i < len(outs):
            saida = outs[i]
        elif canal_extra:
            saida = outs[-1]
        else:
            break
        plano.append((port_path(o.name, saida.name), port_path(d.name, entrada.name)))
    return plano, f"{o.name} ({len(outs)} saidas) -> {d.name} ({len(ins)} entradas)"


def conectar(plano: list[tuple[str, str]], dry_run: bool = True) -> list[tuple[str, str, bool, str]]:
    """Executa (ou só simula) um plano de conexões. Devolve [(saida, entrada, ok, msg)]."""
    feitos: list[tuple[str, str, bool, str]] = []
    for saida, entrada in plano:
        if dry_run:
            feitos.append((saida, entrada, True, "simulado"))
            continue
        ok, msg = link(saida, entrada)
        feitos.append((saida, entrada, ok, msg))
    return feitos


if __name__ == "__main__":
    s = snapshot()
    print(f"{len(s.nodes)} nós, {len(s.ports)} portas, {len(s.links)} conexões")
    print(f"default sink  : {default_sink(s)}")
    print(f"default source: {default_source(s)}")
    print(f"serviços      : {services()}")
    print(f"jack          : {jack_status()}")
    print(f"clock         : {clock(s)}")
    for n in s.audio_nodes():
        print(f"  [{n.id:>3}] {n.media_class:<22} {n.name[:38]:<38} {'(app)' if n.is_app else ''}")
