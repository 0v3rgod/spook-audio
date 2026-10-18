"""Projeto do Carla (.carxp) — leitor e escritor de verdade.

Por que existe: a "rack" do Carla não pode ser montada por linha de comando; ela mora
num arquivo `.carxp`. Para a Fase 2 (Carla como rack dentro da sessão do Ardour) o rack
precisa ser **reprodutível**, então este módulo sabe escrever e ler o formato.

O formato foi transcrito do próprio Carla instalado (2.6.0-alpha1), não inventado:
  * `/usr/lib/carla/libcarla_standalone2.so` → `CarlaEngine::saveProjectInternal`
  * upstream `source/backend/engine/CarlaEngine.cpp` (linhas 2336-2595)
  * upstream `source/utils/CarlaStateUtils.cpp`  (`CarlaStateSave::dumpToMemoryStream`)

Estrutura (o que o engine escreve, identado com 1 espaço):

    <CARLA-PROJECT VERSION='2.6'>
     <EngineSettings> … </EngineSettings>
     <Transport><BeatsPerMinute>…</BeatsPerMinute></Transport>   (opcional)
     <Plugin>
      <Info><Type>…</Type><Name>…</Name> …campos por tipo… </Info>
      <Data><Active>Yes</Active><Options>0x0</Options> <Parameter>…</Parameter> </Data>
     </Plugin>
     <ExternalPatchbay><Connection><Source/><Target/></Connection></ExternalPatchbay>
    </CARLA-PROJECT>

Detalhes que o parser (`CarlaStateSave::fillFromXmlElement`) confirma:
  * `<Label>`/`<URI>`/`<Identifier>`/`<Setup>` **todos** viram `label`;
  * `<Binary>`/`<Bundle>`/`<Filename>` **todos** viram `binary`;
  * a tag raiz precisa ser `carla-project` (ou `carla-preset`).
"""
from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

VERSAO = "2.6"
EXTENSAO = ".carxp"

# tipos de plugin que o Carla hospeda (RE 02) e como o `dumpToMemoryStream` os grava
# campo interno -> tag do <Info>, por tipo
_INFO: dict[str, tuple[tuple[str, str], ...]] = {
    "INTERNAL": (("label", "Label"),),
    "LADSPA":   (("binary", "Binary"), ("label", "Label"), ("uniqueId", "UniqueID")),
    "DSSI":     (("binary", "Binary"), ("label", "Label")),
    "LV2":      (("label", "URI"),),
    "VST2":     (("binary", "Binary"), ("uniqueId", "UniqueID")),
    "VST3":     (("binary", "Binary"), ("label", "Label")),
    "AU":       (("binary", "Bundle"), ("label", "Identifier")),
    "CLAP":     (("binary", "Binary"), ("label", "Identifier")),
    "GIG":      (("binary", "Filename"), ("label", "Label")),
    "SF2":      (("binary", "Filename"), ("label", "Label")),
    "SFZ":      (("binary", "Filename"),),
    "JSFX":     (("binary", "Filename"), ("label", "Label")),
    "JACK":     (("binary", "Filename"), ("label", "Setup")),
}

# plugins INTERNAL que o Carla sempre traz (strings do libcarla_standalone2.so)
INTERNOS = ("audiofile", "bigmeter", "midifile", "midipattern", "notes", "xycontroller")

_TAG_PARA_CAMPO = {"Label": "label", "URI": "label", "Identifier": "label", "Setup": "label",
                   "Binary": "binary", "Bundle": "binary", "Filename": "binary",
                   "UniqueID": "unique_id", "Type": "tipo", "Name": "nome"}


class ProjetoError(RuntimeError):
    pass


# ------------------------------------------------------------------ escapar
def _esc(t: str | None) -> str:
    """Igual ao xmlSafeString(..., true) do Carla."""
    s = "" if t is None else str(t)
    for a, b in (("&", "&amp;"), ("<", "&lt;"), (">", "&gt;"), ("'", "&apos;"), ('"', "&quot;")):
        s = s.replace(a, b)
    return s


def _des(t: str | None) -> str:
    """Igual ao xmlSafeString(..., false)."""
    s = "" if t is None else str(t)
    for a, b in (("&lt;", "<"), ("&gt;", ">"), ("&apos;", "'"), ("&quot;", '"'), ("&amp;", "&")):
        s = s.replace(a, b)
    return s


def _bool(v: bool) -> str:
    return "true" if v else "false"


def _flt(v: float) -> str:
    """String de float como o water::String(x, 7)."""
    return f"{float(v):.7f}".rstrip("0").rstrip(".") or "0"


# ------------------------------------------------------------------ modelos
@dataclass
class Parametro:
    """Um `<Parameter>` (o `dummy=True` significa: só midiChannel/CC são usados)."""
    index: int
    nome: str = ""
    symbol: str = ""
    valor: float = 0.0
    dummy: bool = False
    midi_channel: int = 0
    mapped_control_index: int = -1
    mapped_min: float | None = None
    mapped_max: float | None = None


@dataclass
class Plugin:
    tipo: str = "INTERNAL"
    nome: str = ""
    label: str = ""          # <Label>/<URI>/<Identifier>/<Setup> conforme o tipo
    binary: str = ""         # <Binary>/<Bundle>/<Filename>
    unique_id: int = 0
    ativo: bool = True
    dry_wet: float = 1.0
    volume: float = 1.0
    balanco_esq: float = -1.0
    balanco_dir: float = 1.0
    panning: float = 0.0
    canal_controle: int = -1
    opcoes: int = 0
    audio_ins: int = 2
    audio_outs: int = 2
    parametros: list[Parametro] = field(default_factory=list)
    programa: str | None = None
    custom: list[tuple[str, str, str]] = field(default_factory=list)
    chunk: str | None = None
    comentario: str = ""

    def __post_init__(self):
        self.tipo = self.tipo.upper()
        if self.tipo not in _INFO:
            raise ProjetoError(f"tipo de plugin desconhecido: {self.tipo!r} (use {list(_INFO)})")
        if not self.comentario:
            self.comentario = self.nome or self.label or self.tipo


@dataclass
class Rack:
    plugins: list[Plugin] = field(default_factory=list)
    conexoes: list[tuple[str, str]] = field(default_factory=list)   # (origem, destino)
    bpm: float | None = None
    posicoes: list[dict] = field(default_factory=list)

    def resumo(self) -> str:
        partes = [f"{len(self.plugins)} plugin(s)"]
        if self.plugins:
            partes.append("cadeia: " + " → ".join(p.nome or p.label or p.tipo for p in self.plugins))
        if self.conexoes:
            partes.append(f"{len(self.conexoes)} conexão(ões)")
        return " · ".join(partes)


def interno(label: str, nome: str = "", **kw) -> Plugin:
    """Plugin INTERNAL do Carla (o único tipo sempre disponível nesta máquina)."""
    return Plugin(tipo="INTERNAL", label=label, nome=nome or label.title(), **kw)


# ------------------------------------------------------------------ escrever
def _info_xml(p: Plugin) -> str:
    linhas = ["  <Info>", f"   <Type>{_esc(p.tipo)}</Type>",
              f"   <Name>{_esc(p.nome)}</Name>"]
    for campo, tag in _INFO[p.tipo]:
        if campo == "uniqueId":
            linhas.append(f"   <UniqueID>{int(p.unique_id)}</UniqueID>")
        else:
            valor = getattr(p, campo)
            if campo == "binary" and not valor:
                continue                      # o engine também omite binary vazio?
            linhas.append(f"   <{tag}>{_esc(valor)}</{tag}>")
    linhas.append("  </Info>")
    return "\n".join(linhas) + "\n\n"


def _param_xml(pl: Parametro, tipo: str) -> str:
    b = ["", "   <Parameter>", f"    <Index>{int(pl.index)}</Index>",
         f"    <Name>{_esc(pl.nome)}</Name>"]
    if pl.symbol:
        tag = "Identifier" if tipo == "CLAP" else "Symbol"
        b.append(f"    <{tag}>{_esc(pl.symbol)}</{tag}>")
    if pl.mapped_control_index > 0:
        b.append(f"    <MidiChannel>{int(pl.midi_channel) + 1}</MidiChannel>")
        b.append(f"    <MappedControlIndex>{int(pl.mapped_control_index)}</MappedControlIndex>")
        if pl.mapped_min is not None and pl.mapped_max is not None:
            b.append(f"    <MappedMinimum>{float(pl.mapped_min):.15f}</MappedMinimum>")
            b.append(f"    <MappedMaximum>{float(pl.mapped_max):.15f}</MappedMaximum>")
        if 0 < pl.mapped_control_index < 120:
            b.append(f"    <MidiCC>{int(pl.mapped_control_index)}</MidiCC>")
    if not pl.dummy:
        b.append(f"    <Value>{float(pl.valor):.15f}</Value>")
    b.append("   </Parameter>")
    return "\n".join(b) + "\n"


def _plugin_xml(p: Plugin) -> str:
    b = ["", f" <!-- {_esc(p.comentario)} -->", " <Plugin>", _info_xml(p).rstrip("\n"), "", "  <Data>"]
    b.append(f"   <Active>{'Yes' if p.ativo else 'No'}</Active>")
    if abs(p.dry_wet - 1.0) > 1e-9:
        b.append(f"   <DryWet>{_flt(p.dry_wet)}</DryWet>")
    if abs(p.volume - 1.0) > 1e-9:
        b.append(f"   <Volume>{_flt(p.volume)}</Volume>")
    if abs(p.balanco_esq - (-1.0)) > 1e-9:
        b.append(f"   <Balance-Left>{_flt(p.balanco_esq)}</Balance-Left>")
    if abs(p.balanco_dir - 1.0) > 1e-9:
        b.append(f"   <Balance-Right>{_flt(p.balanco_dir)}</Balance-Right>")
    if abs(p.panning) > 1e-9:
        b.append(f"   <Panning>{_flt(p.panning)}</Panning>")
    b.append(f"   <ControlChannel>{'N' if p.canal_controle < 0 else p.canal_controle + 1}</ControlChannel>")
    b.append(f"   <Options>0x{int(p.opcoes):x}</Options>")
    for pl in p.parametros:
        b.append(_param_xml(pl, p.tipo).rstrip("\n"))
    if p.programa:
        b.extend(["", "   <CurrentProgramIndex>1</CurrentProgramIndex>",
                  f"   <CurrentProgramName>{_esc(p.programa)}</CurrentProgramName>"])
    for tipo, chave, valor in p.custom:
        b.append("")
        b.append("   <CustomData>")
        b.append(f"    <Type>{_esc(tipo)}</Type>")
        b.append(f"    <Key>{_esc(chave)}</Key>")
        if tipo == "chunk" or len(valor) >= 128:
            b.append("    <Value>")
            b.append(_esc(valor))
            b.append("    </Value>")
        else:
            b.append(f"    <Value>{_esc(valor)}</Value>")
        b.append("   </CustomData>")
    if p.chunk:
        b.extend(["", "   <Chunk>", _esc(p.chunk), "   </Chunk>"])
    b.append("  </Data>")
    b.append(" </Plugin>")
    return "\n".join(b) + "\n"


def _patchbay_xml(conexoes: list[tuple[str, str]], posicoes: list[dict]) -> str:
    b = ["", " <ExternalPatchbay>"]
    for origem, destino in conexoes:
        b.extend(["  <Connection>",
                  f"   <Source>{_esc(origem)}</Source>",
                  f"   <Target>{_esc(destino)}</Target>",
                  "  </Connection>"])
    if posicoes:
        b.append("  <Positions>")
        for p in posicoes:
            extra = ""
            if p.get("x2") or p.get("y2"):
                extra += f'" x2="{p.get("x2", 0)}" y2="{p.get("y2", 0)}'
            if p.get("pluginId", -1) >= 0:
                extra += f'" pluginId="{p["pluginId"]}'
            b.append(f'   <Position x1="{p.get("x1", 0)}" y1="{p.get("y1", 0)}{extra}">')
            b.append(f'    <Name>{_esc(p.get("name", ""))}</Name>')
            b.append("   </Position>")
        b.append("  </Positions>")
    b.append(" </ExternalPatchbay>")
    return "\n".join(b) + "\n"


def escrever(caminho: str | Path, rack: Rack | list[Plugin],
             conexoes: list[tuple[str, str]] | None = None,
             bpm: float | None = None, posicoes: list[dict] | None = None) -> tuple[Path, int]:
    """Escreve um `.carxp` no formato canônico do Carla. Devolve (caminho, bytes)."""
    if isinstance(rack, (list, tuple)):
        rack = Rack(plugins=list(rack), conexoes=list(conexoes or []), bpm=bpm,
                    posicoes=list(posicoes or []))
    alvo = Path(caminho)
    if alvo.suffix != EXTENSAO:
        alvo = alvo.with_suffix(EXTENSAO)
    alvo.parent.mkdir(parents=True, exist_ok=True)

    partes = ["<?xml version='1.0' encoding='UTF-8'?>",
              "<!DOCTYPE CARLA-PROJECT>",
              f"<CARLA-PROJECT VERSION='{VERSAO}' IgnoreClientPrefix='true'>",
              " <EngineSettings>",
              "  <ForceStereo>false</ForceStereo>",
              "  <PreferPluginBridges>false</PreferPluginBridges>",
              "  <PreferUiBridges>true</PreferUiBridges>",
              "  <UIsAlwaysOnTop>true</UIsAlwaysOnTop>",
              "  <MaxParameters>200</MaxParameters>",
              "  <UIBridgesTimeout>4000</UIBridgesTimeout>",
              " </EngineSettings>"]
    if rack.bpm:
        partes.extend(["", " <Transport>", f"  <BeatsPerMinute>{_flt(rack.bpm)}</BeatsPerMinute>",
                       " </Transport>"])
    for p in rack.plugins:
        partes.append(_plugin_xml(p).rstrip("\n"))
    if rack.conexoes or rack.posicoes:
        partes.append(_patchbay_xml(rack.conexoes, rack.posicoes).rstrip("\n"))
    partes.append("</CARLA-PROJECT>")
    texto = "\n".join(partes) + "\n"
    alvo.write_text(texto, encoding="utf-8")
    return alvo, len(texto.encode("utf-8"))


# ------------------------------------------------------------------ ler
def _numero(txt: str | None) -> float:
    try:
        return float((txt or "0").strip())
    except ValueError:
        return 0.0


def ler(caminho: str | Path) -> Rack:
    """Le um `.carxp` de volta (usado no teste de ida-e-volta)."""
    txt = Path(caminho).read_text(encoding="utf-8")
    if "<CARLA-PROJECT" not in txt:
        raise ProjetoError(f"não é um projeto Carla: {caminho}")
    raiz = ET.fromstring(re.sub(r"<!DOCTYPE[^>]*>", "", txt))
    if raiz.tag.lower() not in ("carla-project", "carla-preset"):
        raise ProjetoError(f"tag raiz inesperada: {raiz.tag}")

    rack = Rack()
    tr = raiz.find("Transport/BeatsPerMinute")
    if tr is not None and (tr.text or "").strip():
        rack.bpm = _numero(tr.text)

    for el in raiz.findall("Plugin"):
        p = Plugin(tipo="INTERNAL")
        p.tipo, p.nome, p.label, p.binary, p.unique_id = "INTERNAL", "", "", "", 0
        info = el.find("Info")
        if info is not None:
            for filho in info:
                campo = _TAG_PARA_CAMPO.get(filho.tag)
                if campo is None:
                    continue
                valor = _des(filho.text)
                if campo == "unique_id":
                    p.unique_id = int(_numero(valor))
                else:
                    setattr(p, campo, valor)
        dados = el.find("Data")
        if dados is not None:
            for filho in dados:
                tag, texto = filho.tag, (filho.text or "").strip()
                if tag == "Active":
                    p.ativo = texto == "Yes"
                elif tag == "DryWet":
                    p.dry_wet = _numero(texto)
                elif tag == "Volume":
                    p.volume = _numero(texto)
                elif tag == "Balance-Left":
                    p.balanco_esq = _numero(texto)
                elif tag == "Balance-Right":
                    p.balanco_dir = _numero(texto)
                elif tag == "Panning":
                    p.panning = _numero(texto)
                elif tag == "ControlChannel":
                    p.canal_controle = -1 if texto.upper().startswith("N") else int(_numero(texto)) - 1
                elif tag == "Options":
                    p.opcoes = int(texto.replace("0x", ""), 16) if texto else 0
                elif tag == "Parameter":
                    pl = Parametro(index=int(_numero(filho.findtext("Index"))))
                    pl.nome = _des(filho.findtext("Name"))
                    pl.symbol = _des(filho.findtext("Symbol") or filho.findtext("Identifier"))
                    pl.valor = _numero(filho.findtext("Value"))
                    pl.dummy = filho.find("Value") is None
                    pl.midi_channel = int(_numero(filho.findtext("MidiChannel"))) - 1
                    pl.mapped_control_index = int(_numero(filho.findtext("MappedControlIndex", "-1")))
                    p.parametros.append(pl)
                elif tag == "CurrentProgramName":
                    p.programa = _des(texto)
                elif tag == "CustomData":
                    p.custom.append((_des(filho.findtext("Type")), _des(filho.findtext("Key")),
                                     _des(filho.findtext("Value"))))
                elif tag == "Chunk":
                    p.chunk = _des(texto)
        rack.plugins.append(p)

    for el in raiz.findall("ExternalPatchbay/Connection") + raiz.findall("Patchbay/Connection"):
        origem, destino = el.findtext("Source"), el.findtext("Target")
        if origem and destino:
            rack.conexoes.append((_des(origem), _des(destino)))
    for el in raiz.findall("ExternalPatchbay/Positions/Position"):
        d = {"x1": int(_numero(el.get("x1"))), "y1": int(_numero(el.get("y1")))}
        nome = el.findtext("Name")
        if nome:
            d["name"] = _des(nome)
        rack.posicoes.append(d)
    return rack


def validar(caminho: str | Path) -> tuple[bool, str]:
    """Confere o essencial sem depender do Carla: XML, tag raiz, versão e tipos."""
    txt = Path(caminho).read_text(encoding="utf-8")
    if not txt.startswith("<?xml version='1.0' encoding='UTF-8'?>"):
        return False, "cabeçalho XML diferente do que o engine escreve"
    if "<!DOCTYPE CARLA-PROJECT>" not in txt:
        return False, "falta <!DOCTYPE CARLA-PROJECT>"
    if not re.search(rf"<CARLA-PROJECT VERSION='{re.escape(VERSAO)}'", txt):
        return False, f"VERSION esperada {VERSAO!r}"
    try:
        r = ler(Path(caminho))
    except (ET.ParseError, ProjetoError) as e:
        return False, f"{type(e).__name__}: {e}"
    for p in r.plugins:
        if p.tipo not in _INFO:
            return False, f"tipo desconhecido no arquivo: {p.tipo}"
        if p.tipo == "INTERNAL" and p.label not in INTERNOS:
            return False, f"plugin INTERNAL '{p.label}' não é um dos do Carla: {INTERNOS}"
    if "</CARLA-PROJECT>" not in txt.rstrip()[-64:]:
        return False, "arquivo não fecha com </CARLA-PROJECT>"
    return True, r.resumo()


if __name__ == "__main__":
    print("tipos suportados:", ", ".join(_INFO))
    print("INTERNAL sempre presentes:", ", ".join(INTERNOS))
    print(f"extensão: {EXTENSAO} · versão gravada: {VERSAO}")
