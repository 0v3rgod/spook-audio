"""Fase 2 — Carla como rack dentro da sessão modelo do Ardour.

O que a Fase 2 entrega (README): uma sessão de DAW **modelo** e um **rack** do Carla,
os dois reprodutíveis, ligados por pipewire-jack — sem passar pelos diálogos manuais
do Ardour (`Audio/MIDI Setup`) nem por cliques no Carla.

Como funciona (tudo verificado na RE 02/03):
  * `sessao.py`  grava o `.ardour` com `<EngineHints backend="JACK">` + OSC ligado;
  * `projeto.py` grava o `.carxp` (rack do Carla) no formato do próprio engine;
  * os dois sobem sob `pw-jack`, ou seja, viram clientes JACK do **PipeWire**;
  * o roteamento é feito com `pw-link` (o painel já fala PipeWire — RE 04), e não com
    o patchbay interno do Carla, porque quem manda no grafo aqui é o PipeWire.

Regra de convivência: **nada é executado sem `--executar`**. O padrão é dry-run: escreve
os arquivos, lê o grafo do PipeWire (só leitura, `pw-dump`) e imprime o plano. Isso
existe porque o Taylor pediu para não interferir no que estiver tocando no Spotify.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from . import ardour, carla, projeto, pw, sessao

# ------------------------------------------------------------------ o modelo
NOME_SESSAO = "spook-modelo"
NOME_RACK = "spook-rack"
RATE = 48000
BPM = 120.0
PASTA_FASE2 = Path.home() / "spook-audio" / "fase2"

# regex para achar as pontas no grafo do PipeWire (nomes variam com o dispositivo)
PADRAO_SCARLETT = r"scarlett|focusrite|usb.*audio"
PADRAO_CARLA = r"^carla"
PADRAO_ARDOUR = r"ardour"


def rack_modelo(plugins: list[projeto.Plugin] | None = None) -> projeto.Rack:
    """Rack modelo do fluxo da Scarlett Solo.

    Sem plugins terceiros instalados nesta máquina (foram removidos no fim da RE — ver
    `re/INSTALADOS-PARA-RE.md`), o rack se apoia nos plugins **INTERNAL** do Carla, que
    sempre existem. O `bigmeter` serve de invólucro/medidor: o slot onde um processador
    real (LSP/Calf) entra quando o usuário quiser, sem mudar a estrutura.
    """
    if plugins is None:
        plugins = [
            projeto.interno("bigmeter", "Medidor", comentario="Medidor (INTERNAL do Carla)"),
        ]
    return projeto.Rack(plugins=list(plugins), bpm=BPM)


# ------------------------------------------------------------------ inspeção
def preparar_dry(pasta: Path | None = None, destino_sessao: Path | None = None) -> dict:
    """Só olha o disco: o que já existe do kit, sem escrever nada (usado pelo painel)."""
    pasta = Path(pasta) if pasta else PASTA_FASE2
    alvo_rack = pasta / f"{NOME_RACK}.carxp"
    pasta_sess = Path(destino_sessao) if destino_sessao else sessao.destino_padrao(NOME_SESSAO)
    tem_sess = ardour.arquivo_sessao(pasta_sess) is not None
    tem_rack = alvo_rack.is_file()

    pedacos, pendencias = [], []
    if tem_rack:
        pedacos.append("rack ok")
    else:
        pendencias.append("rack")
    if tem_sess:
        d = sessao.ler(pasta_sess)
        pedacos.append(f"sessão ok ({d.get('backend')}, osc={d.get('osc')})")
        if d.get("backend") != "JACK":
            pendencias.append("motor da sessão não é JACK")
    else:
        pendencias.append("sessão")
    estado = " · ".join(pedacos) if pedacos else "nada preparado ainda"
    if pendencias:
        estado += f" — pendente: {', '.join(pendencias)}"
    return {"sessao": pasta_sess if tem_sess else None,
            "pasta_sessao": pasta_sess,
            "rack": alvo_rack if tem_rack else None,
            "pronto": not pendencias,
            "estado": estado}


# ------------------------------------------------------------------ preparar
def preparar(force: bool = False, pasta: Path | None = None,
             destino_sessao: Path | None = None) -> dict:
    """Escreve (ou reaproveita) a sessão modelo e o rack. Não executa nada."""
    pasta = Path(pasta) if pasta else PASTA_FASE2
    pasta.mkdir(parents=True, exist_ok=True)
    avisos: list[str] = []

    pasta_sess, criada, msg_sess = sessao.garantir(NOME_SESSAO, rate=RATE, backend="jack",
                                                  osc=True, destino=destino_sessao)
    avisos += msg_sess
    if criada:
        avisos.append(f"sessão modelo criada: {pasta_sess}")

    alvo_rack = pasta / f"{NOME_RACK}.carxp"
    if alvo_rack.exists() and not force:
        avisos.append(f"rack já existe (não reescrito): {alvo_rack}")
    else:
        alvo_rack, n = projeto.escrever(alvo_rack, rack_modelo())
        ok, detalhe = projeto.validar(alvo_rack)
        avisos.append(f"rack escrito: {alvo_rack} ({n} bytes) — validar: {'ok' if ok else detalhe}")

    return {"sessao": pasta_sess, "rack": alvo_rack, "avisos": avisos}


def comandos(info: dict) -> list[tuple[str, list[str]]]:
    """(nome, argv) para subir as duas pontas — o que o `--executar` faria."""
    return [
        ("carla-rack", carla.comando("rack", info["rack"])),
        ("ardour", ardour.build_command(str(info["sessao"]), new=False, backend="jack")),
    ]


# ------------------------------------------------------------------ patch
def plano(snap: pw.Snapshot | None = None) -> list[dict]:
    """Plano de conexões (só leitura): Scarlett → Carla → saída do sistema.

    Devolve uma lista de passos com o plano de cada trecho. Quando uma ponta não
    existe no grafo (Carla ainda não subiu, Scarlett desplugada), o passo vem com o
    motivo em vez de conexões — nada é inventado.
    """
    snap = snap or pw.snapshot()
    passos: list[dict] = []

    def trecho(de: str, para: str, rotulo: str):
        conns, motivo = pw.plano_par(snap, de, para)
        passos.append({"rotulo": rotulo, "conexoes": conns, "motivo": motivo})

    trecho(PADRAO_SCARLETT, PADRAO_CARLA, "Scarlett → Carla (racks/proc)")
    trecho(PADRAO_CARLA, PADRAO_ARDOUR, "Carla → Ardour (gravar)")
    trecho(PADRAO_ARDOUR, r"alsa_output|Built-in", "Ardour → saída do sistema")
    return passos


def aplicar(planos: list[dict], dry_run: bool = True) -> list[dict]:
    """Aplica o plano com pw-link. dry_run=True só simula (padrão)."""
    resultado: list[dict] = []
    for passo in planos:
        feitos = pw.conectar(passo["conexoes"], dry_run=dry_run)
        resultado.append({**passo, "feitos": feitos})
    return resultado


# ------------------------------------------------------------------ CLI
def _imprime_plano(planos: list[dict]) -> None:
    for p in planos:
        print(f"\n  {p['rotulo']}")
        print(f"    ({p['motivo']})")
        if not p["conexoes"]:
            print("    nada a fazer")
        for saida, entrada in p["conexoes"]:
            print(f"    pw-link {saida}  {entrada}")


def principal(argv: list[str] | None = None) -> int:
    import sys
    argv = list(sys.argv[1:] if argv is None else argv)
    executar = "--executar" in argv
    force = "--force" in argv

    print("FASE 2 — Carla como rack + sessão modelo do Ardour")
    print("=" * 62)
    info = preparar(force=force)
    for a in info["avisos"]:
        print(f"  · {a}")
    print(f"\n  sessão : {info['sessao']}")
    print(f"  rack   : {info['rack']}")

    print("\ncomandos:")
    for nome, cmd in comandos(info):
        print(f"  {nome:<11} {shutil.which(cmd[0]) or cmd[0]!s} {' '.join(cmd[1:])}")

    planos = plano()
    print("\nplano de conexões (leitura do grafo do PipeWire):")
    _imprime_plano(planos)

    if not executar:
        print("\n[dry-run] nada foi executado. Para subir de verdade: "
              "python3 -m spookaudio.fase2 --executar")
        return 0

    # ---- caminho de execução (opt-in) -------------------------------------
    if not carla.instalado():
        print("\nerro: carla-rack não instalado")
        return 2
    proc_c, log_c = carla.launch("rack", info["rack"])
    print(f"\ncarla-rack: pid {proc_c.pid} (log {log_c})")
    proc_a, log_a = ardour.launch(str(info["sessao"]), new=False, backend="jack")
    print(f"ardour: pid {proc_a.pid} (log {log_a})")

    print("\naguardando os clientes JACK aparecerem para aplicar o patch...")
    import time
    for _ in range(20):
        time.sleep(1.0)
        if pw.snapshot().achar(PADRAO_CARLA) and pw.snapshot().achar(PADRAO_ARDOUR):
            break
    for p in aplicar(plano()):
        print(f"\n  {p['rotulo']}")
        for saida, entrada, ok, msg in p["feitos"]:
            print(f"    [{'ok' if ok else 'erro'}] {saida} -> {entrada} {'' if ok else msg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(principal())
