"""Autoteste headless do SPOOK AUDIO (não abre janela).

    python3 spook-audio.py --check
    # ou
    xvfb-run -a python3 -m tests.selftest --gui   # valida também a construção da janela
"""
from __future__ import annotations

import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FALHAS: list[str] = []
OKS: list[str] = []


def check(nome: str, fn):
    try:
        r = fn()
        detalhe = f" — {r}" if isinstance(r, str) and r else ""
        print(f"  [ok]   {nome}{detalhe}")
        return True
    except Exception as e:
        print(f"  [FALHA] {nome}: {type(e).__name__}: {e}")
        import traceback as tb
        print("         " + "\n         ".join(tb.format_exc().strip().splitlines()[-3:]))
        return False


def t_imports() -> str:
    from spookaudio import alsa, ardour, carla, osc, projeto, pw, scarlett, sessao
    return "alsa, ardour, carla, osc, projeto, pw, scarlett, sessao"


def t_osc():
    from spookaudio import osc
    casos = [("/transport_play", (1.0,)), ("/rec_enable_toggle", ()),
             ("/access_action", ("Transport/Record",)), ("/gain", (0.5, 3))]
    for end, args in casos:
        addr, volta = osc.decode(osc.encode(end, *args))
        assert addr == end, f"endereço {addr!r} != {end!r}"
        assert len(volta) == len(args), f"args {volta} != {args}"
    # padding
    assert len(osc.encode("/a", "b")) % 4 == 0
    return f"{len(casos)} mensagens ida-e-volta ok"


def t_alsa_cards():
    from spookaudio import alsa
    cards = alsa.list_cards()
    return f"{len(cards)} cards: " + ", ".join(f"{c.index}:{c.card_id}" for c in cards)


def t_alsa_elems():
    from spookaudio import alsa
    cards = alsa.list_cards()
    if not cards:
        return "sem cards (pulado)"
    with alsa.Ctl(cards[0].index) as ctl:
        els = ctl.refresh()
        assert els, "nenhum controle lido"
        tipos = {}
        for el in els:
            tipos[el.type_name] = tipos.get(el.type_name, 0) + 1
    resumo = ", ".join(f"{k}={v}" for k, v in sorted(tipos.items()))
    return f"{len(els)} controles em hw:{cards[0].index} ({resumo})"


def t_pw():
    from spookaudio import pw
    s = pw.snapshot()
    nos = s.audio_nodes()
    return (f"{len(s.nodes)} nós ({len(nos)} de áudio), {len(s.ports)} portas, {len(s.links)} conexões · "
            f"clock={pw.clock(s)} · jack={pw.jack_status()['disponivel']}")


def t_scarlett():
    from spookaudio import scarlett
    devs = scarlett.find()
    if not devs:
        return "nenhuma plugada (ok — a detecção foi exercitada)"
    return "; ".join(scarlett.resumo(d) for d in devs)


def t_ardour():
    return t_ardour_bin()


def t_ardour_bin():
    from spookaudio import ardour
    cmd = ardour.build_command("teste", new=True, backend="jack")
    return (f"instalado={ardour.installed()} versao={ardour.version()} "
            f"sessoes={len(ardour.sessions())} rodando={ardour.running()} "
            f"comando_jack={' '.join(cmd)}")


def t_osc_probe():
    """Sonda de vida: manda /group/list (o Ardour NAO responde /heartbeat).

    Usa um 'Ardour falso' (UDP local) que responde igual ao OSC deles
    (OSC::send_group_list -> /group/list para o endereco de origem).
    """
    import socket
    import threading
    from spookaudio import ardour, osc
    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.bind(("127.0.0.1", 0))
    porta = srv.getsockname()[1]
    visto: list = []

    def loop():
        for _ in range(8):
            try:
                data, addr = srv.recvfrom(65535)
            except OSError:
                return
            try:
                a, args = osc.decode(data)
            except Exception:
                continue
            visto.append(a)
            if a == "/group/list":
                srv.sendto(osc.encode("/group/list", "none"), addr)

    threading.Thread(target=loop, daemon=True).start()
    t = ardour.Transporte(port=porta).start()
    try:
        ok = t.heartbeat(timeout=4.0)
        assert ok, f"heartbeat falhou (recebido: {visto})"
        assert "/group/list" in visto, f"mandou {visto} em vez de /group/list"
        return f"heartbeat ok · resposta={t.resposta()!r}"
    finally:
        t.close()
        srv.close()


def t_ardour_config():
    """RE 03: o motor NAO vive no config; o OSC vive la (e vem desligado)."""
    from spookaudio import ardour
    cfg = ardour.ler_config()
    assert isinstance(cfg["opcoes"], dict) and isinstance(cfg["protocolos"], dict), "config ilegivel"
    if not ardour.CONFIG.exists():
        return "config do Ardour ainda nao existe (abra o Ardour uma vez)"
    pasta = ardour.default_session_dir()
    assert pasta.is_dir(), f"pasta de sessoes inexistente: {pasta}"
    assert any("Open Sound Control" in p for p in cfg["protocolos"]), "surface OSC ausente no config"
    return (f"pasta={pasta} · OSC={ardour.osc_ativo_texto()} · "
            f"{len(cfg['protocolos'])} protocolos no config")


def t_ardour_engine():
    """Troca o motor numa COPIA da sessao (nao encosta na sessao de verdade)."""
    import shutil
    from pathlib import Path
    from spookaudio import ardour
    ss = ardour.sessions()
    if not ss:
        return "nenhuma sessao no disco ainda — ok"
    tmp = Path("/tmp/opencode/selftest-sess")
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(ss[0], tmp)
    arq = ardour.arquivo_sessao(tmp)
    antes = ardour.engine_hints(tmp).get("backend", "?")
    for alvo in ("jack", "alsa", "dummy"):
        ok, msg = ardour.definir_engine(tmp, alvo)
        assert ok, f"definir_engine({alvo}) falhou: {msg}"
        gravado = ardour.engine_hints(tmp).get("backend")
        assert gravado == ardour.BACKEND_ARDOUR[alvo], f"gravou {gravado!r}, esperava {alvo}"
    return f"{arq.name}: {antes} -> jack/alsa/dummy ok (copia em {tmp})"


def t_carla():
    from spookaudio import carla
    modos = [m for m, (e, _) in carla.MODOS.items() if __import__("shutil").which(e)]
    return f"modos disponíveis: {', '.join(modos)} · porta OSC {carla.OSC_PORT}"


def t_gui():
    import os as _os
    tem_tela = (_os.environ.get("DISPLAY") or _os.environ.get("WAYLAND_DISPLAY")
                or _os.environ.get("GDK_BACKEND") == "broadway")
    if not tem_tela:
        return "sem DISPLAY — pulado (rode sob xvfb-run ou Broadway)"
    import gi
    gi.require_version("Gtk", "4.0")
    from gi.repository import Gtk
    from spookaudio import app as appmod
    from spookaudio.app import Janela
    GTKapp = Gtk.Application(application_id="org.spook.AudioTest")
    criada = {}
    def act(a):
        try:
            w = Janela(a)
            criada["ok"] = True
            criada["abas"] = w.nb.get_n_pages()
            a.quit()
        except Exception as e:
            criada["erro"] = f"{type(e).__name__}: {e}"
            a.quit()
    GTKapp.connect("activate", act)
    GTKapp.run([])
    if "erro" in criada:
        raise AssertionError(criada["erro"])
    assert criada.get("ok"), "a janela nao foi construida"
    return f"janela construída com {criada['abas']} abas"


def _grafo_falso() -> list[dict]:
    """pw-dump sintético: Scarlett (1 saída) -> Carla (2 entradas) -> Ardour -> saída.

    Montado à mão para o teste do pareamento rodar **sem** hardware plugado e sem
    encostar no PipeWire de verdade (nada de `pw-link` aqui).
    """
    def no(i, nome, classe, desc=""):
        return {"id": i, "type": "PipeWire:Interface:Node",
                "info": {"state": "running", "props": {"node.name": nome,
                                                       "node.description": desc or nome,
                                                       "media.class": classe}}}

    def porta(i, dono, nome, direcao):
        return {"id": i, "type": "PipeWire:Interface:Port",
                "info": {"props": {"node.id": dono, "port.name": nome,
                                   "port.direction": direcao,
                                   "audio.channel": nome[-1:].upper()}}}

    return [
        no(10, "alsa_input.usb-Focusrite_Scarlett_Solo_4th_Gen-00.analog-stereo",
           "Audio/Source", "Scarlett Solo 4th Gen"),
        porta(11, 10, "capture_AUX0", "out"),
        no(20, "carla", "Audio/Duplex", "Carla"),
        porta(21, 20, "input_1", "in"), porta(22, 20, "input_2", "in"),
        porta(23, 20, "output_1", "out"), porta(24, 20, "output_2", "out"),
        no(30, "ardour", "Audio/Duplex", "Ardour"),
        porta(31, 30, "audio_in 1", "in"), porta(32, 30, "audio_in 2", "in"),
        porta(33, 30, "audio_out 1", "out"),
        no(40, "alsa_output.pci-0000_00_1f.3.HiFi__Headphones__sink",
           "Audio/Sink", "Headphones"),
        porta(41, 40, "playback_FL", "in"), porta(42, 40, "playback_FR", "in"),
    ]


def t_sessao_modelo() -> str:
    """Gera a sessão modelo num /tmp e lê de volta (não abre o Ardour)."""
    import shutil
    import xml.etree.ElementTree as ET
    from pathlib import Path
    from spookaudio import sessao
    ok, msg = sessao.template_ok()
    assert ok, f"template inválido: {msg}"
    tmp = Path("/tmp/opencode/selftest-sessao")/"modelo"
    shutil.rmtree(tmp.parent, ignore_errors=True)
    pasta, avisos = sessao.criar("modelo", destino=tmp, rate=48000, backend="jack", osc=True)
    arq = pasta/"modelo.ardour"
    assert arq.is_file(), f"sessão não gravada: {arq}"
    ET.parse(arq)                                    # XML válido?
    d = sessao.ler(pasta)
    assert d["nome"] == "modelo", d["nome"]
    assert d["backend"] == "JACK", d["backend"]
    assert d["osc"] is True, d["osc"]
    assert d["rate"] == "48000", d["rate"]
    assert d["versao"] == "7003", d["versao"]
    assert d["master"] and not d["trilhas"], d["rotas"]
    assert not sessao.nome_valido("../etc"), "aceitou nome com path"
    return f"{arq.name}: motor={d['backend']} osc={d['osc']} rate={d['rate']} master=ok"


def t_projeto_carxp() -> str:
    """Ida-e-volta do .carxp nos tipos verificados na RE (INTERNAL/LV2/LADSPA)."""
    import shutil
    from pathlib import Path
    from spookaudio import projeto as pj
    tmp = Path("/tmp/opencode/selftest-carxp")
    shutil.rmtree(tmp, ignore_errors=True)
    rack = pj.Rack(
        plugins=[
            pj.interno("bigmeter", "Medidor", volume=0.8,
                       parametros=[pj.Parametro(index=0, nome="Level", valor=0.5)]),
            pj.Plugin(tipo="LV2", nome="CarlaPatchbay",
                      label="http://kxstudio.sf.net/carla/plugins/carlapatchbay"),
            pj.Plugin(tipo="LADSPA", nome="Amp", label="amp_mono",
                      binary="/usr/lib/ladspa/amp.so", unique_id=1048,
                      parametros=[pj.Parametro(index=0, nome="Gain", symbol="gain", valor=0.7)]),
        ],
        conexoes=[("system:capture_1", "carla:input_1")], bpm=120.0)
    alvo, n = pj.escrever(tmp/"t.carxp", rack)
    txt = alvo.read_text(encoding="utf-8")
    linhas = txt.splitlines()
    # o layout é o que o engine escreve (saveProjectInternal): 1 espaço por nível
    assert linhas[0] == "<?xml version='1.0' encoding='UTF-8'?>", linhas[0]
    assert linhas[1] == "<!DOCTYPE CARLA-PROJECT>", linhas[1]
    assert linhas[2].startswith("<CARLA-PROJECT VERSION='2.6'"), linhas[2]
    assert linhas[-1] == "</CARLA-PROJECT>", linhas[-1]
    assert " <EngineSettings>" in linhas and "  <ForceStereo>false</ForceStereo>" in linhas
    assert txt.count(" <Plugin>") == 3 and txt.count(" </Plugin>") == 3, txt.count(" </Plugin>")
    assert txt.count("  <Info>") == 3 and txt.count("  <Data>") == 3
    ok, detalhe = pj.validar(alvo)
    assert ok, detalhe
    lido = pj.ler(alvo)
    assert len(lido.plugins) == 3, len(lido.plugins)
    assert [p.tipo for p in lido.plugins] == ["INTERNAL", "LV2", "LADSPA"], lido.plugins
    assert lido.plugins[0].volume == 0.8, lido.plugins[0].volume
    assert lido.plugins[2].unique_id == 1048, lido.plugins[2].unique_id
    assert lido.plugins[2].parametros[0].valor == 0.7, lido.plugins[2].parametros[0].valor
    assert lido.conexoes == [("system:capture_1", "carla:input_1")], lido.conexoes
    assert lido.bpm == 120.0, lido.bpm
    assert pj.INTERNOS, "sem plugins INTERNAL conhecidos"
    return f"{n} bytes · {lido.resumo()}"


def t_pw_patch() -> str:
    """Pareamento de canais no grafo sintético (mono -> estéreo) e dry-run de verdade."""
    from spookaudio import pw
    snap = pw.snapshot(_grafo_falso())
    plano, motivo = pw.plano_par(snap, r"scarlett|focusrite", r"^carla")
    assert len(plano) == 2, f"esperava 2 conexões (fan-out mono->estéreo), veio {plano}"
    assert {o for o, _ in plano} == {"alsa_input.usb-Focusrite_Scarlett_Solo_4th_Gen-00.analog-stereo:capture_AUX0"}
    assert {d for _, d in plano} == {"carla:input_1", "carla:input_2"}, plano

    # o pw-link NÃO pode ser chamado em dry-run
    original = pw.link
    pw.link = lambda *a, **k: (_ for _ in ()).throw(AssertionError("chamou pw-link em dry-run"))
    try:
        feitos = pw.conectar(plano, dry_run=True)
    finally:
        pw.link = original
    assert all(ok and msg == "simulado" for _, _, ok, msg in feitos), feitos

    vazio, motivo2 = pw.plano_par(snap, r"nao-existe-aqui", r"^carla")
    assert not vazio and "nao encontrada" in motivo2, motivo2
    return f"{len(plano)} conexões (mono→estéreo), dry-run não chamou pw-link · {motivo}"


def t_fase2() -> str:
    """Kit da Fase 2 inteiro em /tmp: sessão modelo + rack + plano (sem executar nada)."""
    import shutil
    from pathlib import Path
    from spookaudio import fase2, projeto, pw
    tmp = Path("/tmp/opencode/selftest-fase2")
    shutil.rmtree(tmp, ignore_errors=True)
    info = fase2.preparar(pasta=tmp, destino_sessao=tmp/"spook-modelo")
    assert info["sessao"].is_dir(), info["sessao"]
    assert (info["sessao"]/f"{fase2.NOME_SESSAO}.ardour").is_file(), "sem .ardour"
    ok, detalhe = projeto.validar(info["rack"])
    assert ok, detalhe

    cmds = fase2.comandos(info)
    assert len(cmds) == 2, cmds
    assert "carla-rack" in " ".join(cmds[0][1]), cmds[0]
    assert "ardour" in " ".join(cmds[1][1]), cmds[1]

    # plano contra o grafo sintético: Scarlett -> Carla -> Ardour -> saída
    passos = fase2.plano(pw.snapshot(_grafo_falso()))
    assert len(passos) == 3, passos
    assert len(passos[0]["conexoes"]) == 2, passos[0]   # 1 saída da Scarlett -> 2 entradas
    assert len(passos[1]["conexoes"]) == 2, passos[1]
    assert len(passos[2]["conexoes"]) == 2, passos[2]   # master mono -> FL/FR
    aplicado = fase2.aplicar(passos, dry_run=True)
    assert all(f[2] for p in aplicado for f in p["feitos"]), aplicado
    return f"{info['rack'].name} ok · {len(passos)} trechos de patch · dry-run"


CASOS = [
    ("imports dos módulos", t_imports),
    ("OSC ida-e-volta", t_osc),
    ("ALSA: cards", t_alsa_cards),
    ("ALSA: enumeração de controles", t_alsa_elems),
    ("PipeWire: grafo", t_pw),
    ("Scarlett: detecção", t_scarlett),
    ("Ardour: binário/sessões/comando", t_ardour),
    ("OSC: sonda no Ardour (fake)", t_osc_probe),
    ("Ardour: config (OSC/pasta)", t_ardour_config),
    ("Ardour: motor da sessão", t_ardour_engine),
    ("Carla: modos/OSC", t_carla),
    ("Sessão modelo do Ardour", t_sessao_modelo),
    ("Projeto do Carla (.carxp)", t_projeto_carxp),
    ("PipeWire: patch por canal", t_pw_patch),
    ("Fase 2: sessão+rack+plano", t_fase2),
    ("GTK: construção da janela", t_gui),
]


def main() -> int:
    print("SPOOK AUDIO — autoteste")
    print("=" * 62)
    falhas = 0
    for nome, fn in CASOS:
        try:
            r = fn()
            print(f"  ok   {nome:<38} {r}")
        except AssertionError as e:
            falhas += 1
            print(f"  FALHA {nome:<38} {e}")
        except Exception as e:
            falhas += 1
            print(f"  ERRO  {nome:<38} {type(e).__name__}: {e}")
            import traceback
            print("        " + "\n        ".join(traceback.format_exc().strip().splitlines()[-3:]))
    print("=" * 62)
    print(f"{len(CASOS) - falhas}/{len(CASOS)} ok" + ("" if not falhas else f"  ({falhas} com problema)"))
    return 1 if falhas else 0


if __name__ == "__main__":
    raise SystemExit(main())
