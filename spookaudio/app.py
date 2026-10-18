"""SPOOK AUDIO — janela GTK4: orquestra PipeWire + Ardour + Carla + Scarlett.

Regra de convivência: abrir só quando o usuário mandar. Para teste, use
`xvfb-run -a python3 spook-audio.py` (não usa a sessão Wayland dele).
"""
from __future__ import annotations

import shutil
import subprocess
import threading
import time
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import GLib, Gtk  # noqa: E402

from . import alsa, ardour, carla, fase2, projeto, pw, scarlett

CSS = """
window { background: #05070A; }
headerbar { background: #080D12; }
.cartao { background: #0B1016; border: 1px solid #1E2A35; border-radius: 8px; padding: 10px; }
.titulo { color: #9FB0BE; font-weight: 700; letter-spacing: 1px; }
.sub { color: #7E8B97; }
.valor { color: #00E0A0; font-weight: 600; }
.aviso { color: #FFD400; }
.erro { color: #FF3B3B; }
.off { color: #6E7A85; }
chave { }
button { background: #121A22; color: #DCE6EE; border: 1px solid #24313C; border-radius: 6px; padding: 6px 10px; }
button:hover { background: #182430; }
button.destaque { background: #0E3A2A; border-color: #17795A; color: #7DFFC4; font-weight: 600; }
button.perigo { background: #3A1418; border-color: #7A2530; color: #FF9AA5; }
entry { background: #0A1218; color: #E8F1F5; border: 1px solid #24313C; }
textview, textview text { background: #05070A; color: #CFE3EF; }
scale trough { background: #16212B; }
scale highlight { background: #17795A; }
notebook tab { color: #8A97A3; }
notebook tab:checked { color: #00E0A0; }
"""


def _esc(t) -> str:
    return str(t).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Janela(Gtk.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title="SPOOK AUDIO")
        self.set_default_size(1120, 780)

        prov = Gtk.CssProvider()
        prov.load_from_string(CSS)
        Gtk.StyleContext.add_provider_for_display(self.get_display(), prov,
                                                  Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        self.trans = ardour.Transporte(ao_receber=self._osc_feedback)
        self.trans.start()
        self.fb: dict = {}          # preenchido pela thread do OSC; lido no _atualiza_status
        # sonda de vida na abertura (em thread, para nao travar a janela)
        threading.Thread(target=self.trans.heartbeat, kwargs={"timeout": 1.2},
                         daemon=True).start()
        self.trans.start()
        self.ctl_alsa: alsa.Ctl | None = None
        self.dev: dict | None = None
        self._msgs: list[str] = []

        self.lbl_cab = Gtk.Label()
        self.lbl_cab.set_markup('<span font_family="monospace" weight="bold">SPOOK // AUDIO</span>')
        cab = Gtk.HeaderBar()
        cab.set_title_widget(self.lbl_cab)
        self.set_titlebar(cab)

        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.set_child(raiz)

        nb = Gtk.Notebook()
        nb.set_vexpand(True)
        self.nb = nb
        raiz.append(self.nb)
        self.nb.append_page(self._pagina_sessao(), Gtk.Label(label="SESSÃO"))
        self.nb.append_page(self._pagina_scarlett(), Gtk.Label(label="SCARLETT"))
        self.nb.append_page(self._pagina_patchbay(), Gtk.Label(label="PATCHBAY"))
        self.nb.append_page(self._pagina_fase2(), Gtk.Label(label="FASE 2"))
        self.nb.append_page(self._pagina_ferramentas(), Gtk.Label(label="FERRAMENTAS"))

        raiz.append(self._rodape())

        self._atualiza_status()
        GLib.timeout_add_seconds(3, self._tick)
        self._log("SPOOK AUDIO iniciado.")
        self._patchbay_refresh()

    # ----------------------------------------------------------------- helpers
    def _cartao(self, titulo: str) -> Gtk.Box:
        caixa = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        caixa.add_css_class("cartao")
        lbl = Gtk.Label(label=titulo.upper())
        lbl.set_xalign(0)
        lbl.add_css_class("titulo")
        caixa.append(lbl)
        return caixa

    def _campo(self, chave: str) -> tuple[Gtk.Box, Gtk.Label]:
        linha = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        k = Gtk.Label(label=chave)
        k.set_xalign(0)
        k.set_size_request(160, -1)
        k.add_css_class("sub")
        v = Gtk.Label()
        v.set_xalign(0)
        v.set_selectable(True)
        v.set_text("—")
        v.add_css_class("valor")
        linha.append(k)
        linha.append(v)
        return linha, v

    def _definir(self, lbl: Gtk.Label, texto: str, cls: str = "valor") -> None:
        """Escreve o valor no label usando classe CSS (markup com class= o GTK recusa)."""
        for c in ("valor", "aviso", "erro", "sub", "off"):
            lbl.remove_css_class(c)
        lbl.add_css_class(cls)
        lbl.set_text(str(texto))

    def _log(self, txt: str) -> None:
        self._msgs.append(f"{time.strftime('%H:%M:%S')}  {txt}")
        del self._msgs[:-400]
        if getattr(self, "txt_log", None) is not None:
            buf = self.txt_log.get_buffer()
            buf.set_text("\n".join(self._msgs))
            mark = buf.create_mark(None, buf.get_end_iter(), False)
            self.txt_log.scroll_mark_onscreen(mark)

    def _rodape(self) -> Gtk.Widget:
        self.lbl_rodape = Gtk.Label()
        self.lbl_rodape.set_xalign(0)
        self.lbl_rodape.add_css_class("sub")
        caixa = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        caixa.set_margin_start(10)
        caixa.set_margin_end(10)
        caixa.set_margin_bottom(6)
        caixa.append(self.lbl_rodape)
        return caixa

    # -------------------------------------------------------------- SESSÃO
    def _pagina_base(self) -> Gtk.Box:
        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        for lado in ("top", "bottom", "start", "end"):
            getattr(raiz, f"set_margin_{lado}")(10)
        return raiz

    def _pagina_sessao(self) -> Gtk.Widget:
        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        raiz.set_margin_top(10); raiz.set_margin_bottom(10)
        raiz.set_margin_start(10); raiz.set_margin_end(10)

        c1 = self._cartao("estado do sistema")
        for chave, attr in (("PipeWire", "l_pw"), ("JACK (pipewire-jack)", "l_pwj"),
                            ("taxa / quantum", "l_clk"), ("saída padrão", "l_sink"),
                            ("entrada padrão", "l_src")):
            linha, v = self._campo(chave)
            setattr(self, attr, v)
            c1.append(linha)
        raiz.append(c1)

        c2 = self._cartao("ardour")
        linha, v = self._campo("binário"); self.l_ard = v; c2.append(linha)
        linha, v = self._campo("estado"); self.l_ardst = v; c2.append(linha)

        lm = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        k = Gtk.Label(label="motor"); k.set_xalign(0); k.set_size_request(160, -1); k.add_css_class("sub")
        self.dd_backend = Gtk.DropDown.new_from_strings(
            [f"{b}  ·  {ardour.BACKEND_NOTES[b]}" for b in ardour.BACKENDS])
        lm.append(k); lm.append(self.dd_backend)
        c2.append(lm)

        ls = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        k2 = Gtk.Label(label="sessão"); k2.set_xalign(0); k2.set_size_request(160, -1); k2.add_css_class("sub")
        self.en_sessao = Gtk.Entry()
        self.en_sessao.set_placeholder_text("nome da sessão (ou nova)")
        self.en_sessao.set_hexpand(True)
        ls.append(k2); ls.append(self.en_sessao)
        c2.append(ls)

        # sessoes encontradas + motor gravado na sessao (RE 03): o Ardour NAO guarda o
        # motor no config global — grava <EngineHints backend="..."/> DENTRO da sessao.
        self._sessoes: list = []
        self._nomes_sess: list = []
        ld = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        kd = Gtk.Label(label="encontradas")
        kd.set_xalign(0); kd.set_size_request(160, -1); kd.add_css_class("sub")
        self.dd_sess = Gtk.DropDown.new_from_strings(["—"])
        self.dd_sess.set_hexpand(True)
        self.dd_sess.connect("notify::selected", self._on_sessao)
        ld.append(kd); ld.append(self.dd_sess)
        c2.append(ld)

        lmt = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        km = Gtk.Label(label="motor da sessão"); km.set_xalign(0); km.set_size_request(160, -1); km.add_css_class("sub")
        self.l_motor_sess = Gtk.Label(label="—")
        self.l_motor_sess.set_xalign(0); self.l_motor_sess.set_hexpand(True)
        self.l_motor_sess.add_css_class("valor")
        bt_eng = Gtk.Button(label="Gravar motor na sessão")
        bt_eng.set_tooltip_text("Grava o motor escolhido no <EngineHints> da sessão (Ardour "
                                "fechado). É o que substitui o diálogo 'Audio/MIDI Setup'.")
        bt_eng.connect("clicked", self._gravar_motor)
        lmt.append(km); lmt.append(self.l_motor_sess); lmt.append(bt_eng)
        c2.append(lmt)

        # OSC vem DESLIGADO por padrao; sem ele o transporte nao responde nada.
        lo = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        ko = Gtk.Label(label="OSC (transporte)")
        ko.set_xalign(0); ko.set_size_request(160, -1); ko.add_css_class("sub")
        self.l_osc = Gtk.Label(label="—")
        self.l_osc.set_xalign(0); self.l_osc.set_hexpand(True); self.l_osc.add_css_class("valor")
        bt_osc = Gtk.Button(label="Ligar OSC")
        bt_osc.set_tooltip_text("Liga a surface 'Open Sound Control (OSC)' em "
                                "~/.config/ardour9/config (o Ardour precisa estar fechado).")
        bt_osc.connect("clicked", self._ligar_osc)
        lo.append(ko); lo.append(self.l_osc); lo.append(bt_osc)
        c2.append(lo)

        lb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for rotulo, cb, cls in (("▶  Abrir sessão", self._abrir_sessao, "destaque"),
                                ("✚  Nova sessão", self._nova_sessao, ""),
                                ("■  Parar Ardour", self._parar_ardour, ""),
                                ("♥  Testar OSC", self._testar_osc, "")):
            b = Gtk.Button(label=rotulo)
            if cls:
                b.add_css_class(cls)
            b.connect("clicked", cb)
            lb.append(b)
        c2.append(lb)
        self._refresh_sessoes()

        dica = Gtk.Label(label=("as sessões ficam em " + str(ardour.default_session_dir())
                                + "  ·  o motor é gravado dentro de cada sessão "
                                  "(troque abaixo, com o Ardour fechado)"))
        dica.set_xalign(0); dica.set_wrap(True); dica.add_css_class("sub")
        c2.append(dica)
        raiz.append(c2)

        c3 = self._cartao("transporte (OSC udp/3819)")
        grade = Gtk.Grid(column_spacing=6, row_spacing=6)
        botoes = [("⏮ Início", "inicio", ""), ("● Gravar", "gravar", "perigo"),
                  ("▶ Play", "play", "destaque"), ("■ Stop", "stop", ""),
                  ("⏭ Fim", "fim", ""), ("🔁 Loop", "loop", ""),
                  ("⚑ Marcador", "marcador", ""), ("⟲ Refazer", "refazer", ""),
                  ("↺ Desfazer", "desfazer", ""), ("💾 Salvar", "salvar", ""),
                  ("⏱ Punch", "punch", ""), ("🎵 Metrônomo", "metronomo", "")]
        for i, (rotulo, acao, cls) in enumerate(botoes):
            b = Gtk.Button(label=rotulo)
            if cls:
                b.add_css_class(cls)
            b.set_hexpand(True)
            b.connect("clicked", lambda _w, a=acao: self._enviar(a))
            grade.attach(b, i % 4, i // 4, 1, 1)
        c3.append(grade)
        # feedback que o Ardour devolve (chega no socket de origem — ver RE 03)
        self.l_fb = Gtk.Label(label="feedback: —")
        self.l_fb.set_xalign(0)
        self.l_fb.set_wrap(True)
        self.l_fb.add_css_class("sub")
        c3.append(self.l_fb)
        raiz.append(c3)

        c4 = self._cartao("registro")
        sw = Gtk.ScrolledWindow()
        sw.set_min_content_height(170)
        self.txt_log = Gtk.TextView()
        self.txt_log.set_monospace(True)
        self.txt_log.set_editable(False)
        self.txt_log.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        sw.set_child(self.txt_log)
        c4.append(sw)
        raiz.append(c4)
        return raiz

    # -------------------------------------------------------------- SCARLETT
    def _pagina_scarlett(self) -> Gtk.Widget:
        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        raiz.set_margin_top(10); raiz.set_margin_bottom(10)
        raiz.set_margin_start(10); raiz.set_margin_end(10)

        c = self._cartao("interface Focusrite Scarlett")
        linha, v = self._campo("detectada"); self.l_scar = v; c.append(linha)
        bt = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for rotulo, cb in (("⟳ Redetectar", self._scarlett_refresh),
                           ("💾 Salvar estado", self._scarlett_store),
                           ("📂 Restaurar estado", self._scarlett_restore)):
            b = Gtk.Button(label=rotulo)
            b.connect("clicked", cb)
            bt.append(b)
        c.append(bt)
        self.l_msg_scar = Gtk.Label()
        self.l_msg_scar.set_xalign(0)
        self.l_msg_scar.set_wrap(True)
        self.l_msg_scar.add_css_class("sub")
        c.append(self.l_msg_scar)
        raiz.append(c)

        sw = Gtk.ScrolledWindow()
        sw.set_vexpand(True)
        self.box_ctrl = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        sw.set_child(self.box_ctrl)
        raiz.append(sw)
        GLib.idle_add(self._scarlett_refresh)
        return raiz

    def _scarlett_refresh(self, *_):
        while (f := self.box_ctrl.get_first_child()) is not None:
            self.box_ctrl.remove(f)
        if self.ctl_alsa:
            try:
                self.ctl_alsa.close()
            except Exception:
                pass
            self.ctl_alsa = None
        devs = scarlett.find()
        self.dev = devs[0] if devs else None
        if not self.dev:
            self._definir(self.l_scar, "nenhuma Scarlett plugada", "aviso")
            self.l_msg_scar.set_text("A identificação usa o usbid do ALSA (/proc/asound/cardN/usbid) — "
                                     "mesmo caminho do alsa-scarlett-gui.")
            return False
        card = self.dev["card"]
        self._definir(self.l_scar, scarlett.resumo(self.dev))
        try:
            self.ctl_alsa = alsa.Ctl(card.index)
            els = self.ctl_alsa.refresh()
        except alsa.AlsaError as e:
            self.l_msg_scar.set_text(f"erro abrindo o mixer ALSA: {e}")
            return False
        self.l_msg_scar.set_text(f"{len(els)} controles lidos de hw:{card.index} (card {card.card_id}).")
        grupos: dict[str, list] = {}
        for el in els:
            grupos.setdefault(scarlett.grupo(el.name), []).append(el)
        for g in [g for g, _ in scarlett.GRUPOS] + ["Outros"]:
            if g not in grupos:
                continue
            cartao = self._cartao(g)
            for el in grupos[g]:
                cartao.append(self._widget_controle(el))
            self.box_ctrl.append(cartao)
        self._log(f"Scarlett: {scarlett.resumo(self.dev)} — {len(els)} controles")
        return False

    def _widget_controle(self, el: alsa.Element) -> Gtk.Widget:
        linha = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        nome = Gtk.Label(label=scarlett.friendly(el.name) or el.name)
        nome.set_xalign(0)
        nome.set_size_request(240, -1)
        nome.set_ellipsize(3)          # Pango.EllipsizeMode.END
        nome.set_tooltip_text(f"{el.name}  [{el.numid}]  {el.type_name}  readonly={not el.writable}")
        linha.append(nome)
        try:
            if el.etype == alsa.T_ENUMERATED:
                dd = Gtk.DropDown.new_from_strings(el.items or ["—"])
                if el.values:
                    dd.set_selected(max(0, min(int(el.values[0]), len(el.items or ["—"]) - 1)))
                dd.set_hexpand(True)
                dd.set_sensitive(el.writable)
                dd.connect("notify::selected", self._on_enum, el, dd)
                linha.append(dd)
            elif el.etype == alsa.T_BOOLEAN:
                sw = Gtk.Switch()
                sw.set_active(bool(el.values[0]) if el.values else False)
                sw.set_halign(Gtk.Align.START)
                sw.set_sensitive(el.writable)
                sw.connect("state-set", self._on_switch, el)
                linha.append(sw)
            else:
                val = el.values[0] if el.values else el.minimum
                step = el.step or max(1, (el.maximum - el.minimum) // 100)
                adj = Gtk.Adjustment(value=val, lower=el.minimum, upper=el.maximum,
                                     step_increment=step, page_increment=step * 10)
                sc = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=adj)
                sc.set_draw_value(True)
                sc.set_digits(0)
                sc.set_hexpand(True)
                sc.set_sensitive(el.writable)
                sc.connect("value-changed", self._on_escala, el)
                linha.append(sc)
        except Exception as e:
            lbl = Gtk.Label(label=f"<{type(e).__name__}: {e}>")
            linha.append(lbl)
        return linha

    def _on_enum(self, dd, _pspec, el: alsa.Element):
        if not self.ctl_alsa:
            return
        idx = int(dd.get_selected())
        try:
            self.ctl_alsa.set_value(el, 0, idx)
            texto = el.items[idx] if idx < len(el.items) else str(idx)
            self._log(f"{el.name} → {texto}")
        except alsa.AlsaError as e:
            self._log(f"erro em {el.name}: {e}")

    def _on_switch(self, sw, estado, el: alsa.Element):
        if not self.ctl_alsa:
            return False
        try:
            self.ctl_alsa.set_value(el, 0, 1 if estado else 0)
            self._log(f"{el.name} → {'ligado' if estado else 'desligado'}")
        except alsa.AlsaError as e:
            self._log(f"erro em {el.name}: {e}")
        return False

    def _on_escala(self, escala, el: alsa.Element):
        if not self.ctl_alsa:
            return
        try:
            self.ctl_alsa.set_value(el, 0, int(escala.get_value()))
        except alsa.AlsaError as e:
            self._log(f"erro em {el.name}: {e}")

    def _scarlett_store(self, *_):
        if not self.dev:
            return
        i = self.dev["card"].index
        r = subprocess.run(["alsactl", "store", str(i)], capture_output=True, text=True)
        self._log(f"alsactl store {i}: " + ("ok" if r.returncode == 0 else (r.stderr or r.stdout).strip()[:120]))

    def _scarlett_restore(self, *_):
        if not self.dev:
            return
        i = self.dev["card"].index
        r = subprocess.run(["alsactl", "restore", str(i)], capture_output=True, text=True)
        self._log(f"alsactl restore {i}: " + ("ok" if r.returncode == 0 else (r.stderr or r.stdout).strip()[:120]))
        self._scarlett_refresh()

    # -------------------------------------------------------------- PATCHBAY
    def _pagina_patchbay(self) -> Gtk.Widget:
        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        raiz.set_margin_top(10); raiz.set_margin_bottom(10)
        raiz.set_margin_start(10); raiz.set_margin_end(10)
        c = self._cartao("grafo do PipeWire")
        bt = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        for rotulo, cb in (("⟳ Atualizar", self._patchbay_refresh),
                           ("qpwgraph", lambda *_: self._abrir("qpwgraph")),
                           ("Helvum", lambda *_: self._abrir("helvum")),
                           ("Carla patchbay", lambda *_: self._abrir_carla("patchbay"))):
            b = Gtk.Button(label=rotulo)
            b.connect("clicked", cb)
            bt.append(b)
        c.append(bt)
        linha, v = self._campo("resumo"); self.l_grafo = v; c.append(linha)
        raiz.append(c)
        sw = Gtk.ScrolledWindow()
        sw.set_vexpand(True)
        self.txt_grafo = Gtk.TextView()
        self.txt_grafo.set_monospace(True)
        self.txt_grafo.set_editable(False)
        sw.set_child(self.txt_grafo)
        raiz.append(sw)
        return raiz

    def _patchbay_refresh(self, *_):
        s = pw.snapshot()
        nos = s.audio_nodes()
        linhas = [f"{len(s.nodes)} nós · {len(s.ports)} portas · {len(s.links)} conexões", ""]
        for n in nos:
            tipo = "SINK" if n.is_sink else ("SRC" if n.is_source else "APP")
            linhas.append(f"[{n.id:>3}] {tipo:<4} {n.media_class:<20} {n.name}")
            linhas.append(f"       {n.desc}")
        linhas.append("")
        linhas.append("conexões:")
        for l in s.links:
            a, b = s.nodes.get(l.out_node), s.nodes.get(l.in_node)
            pa, pb = s.ports.get(l.out_port), s.ports.get(l.in_port)
            if a and b:
                linhas.append(f"  {a.name}:{pa.name if pa else '?'}  →  {b.name}:{pb.name if pb else '?'}")
        self.txt_grafo.get_buffer().set_text("\n".join(linhas))
        self._definir(self.l_grafo, f"{len(nos)} nós de áudio · {len(s.links)} conexões")
        return False

    # -------------------------------------------------------------- FERRAMENTAS
    def _pagina_ferramentas(self) -> Gtk.Widget:
        raiz = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        raiz.set_margin_top(10); raiz.set_margin_bottom(10)
        raiz.set_margin_start(10); raiz.set_margin_end(10)
        c = self._cartao("abrir")
        grade = Gtk.Grid(column_spacing=8, row_spacing=8)
        itens = [("Ardour (DAW)", lambda: self._abrir("ardour9")),
                 ("alsa-scarlett-gui", lambda: self._abrir("alsa-scarlett-gui")),
                 ("Carla · Rack", lambda: self._abrir_carla("rack")),
                 ("Carla · Patchbay", lambda: self._abrir_carla("patchbay")),
                 ("qpwgraph", lambda: self._abrir("qpwgraph")),
                 ("Helvum", lambda: self._abrir("helvum")),
                 ("pavucontrol", lambda: self._abrir("pavucontrol")),
                 ("qjackctl", lambda: self._abrir("qjackctl")),
                 ("Audacity", lambda: self._abrir("audacity")),
                 ("Hydrogen", lambda: self._abrir("hydrogen")),
                 ("Guitarix", lambda: self._abrir("guitarix")),
                 ("EasyEffects", lambda: self._abrir("easyeffects")),
                 ("Qtractor", lambda: self._abrir("qtractor")),
                 ("VMPK (teclado MIDI)", lambda: self._abrir("vmpk")),
                 ("a2jmidid (ponte MIDI)", lambda: self._abrir("a2jmidid", ["-e"])),
                 ("Terminal", lambda: self._abrir("kitty"))]
        for i, (rotulo, cb) in enumerate(itens):
            b = Gtk.Button(label=rotulo)
            b.connect("clicked", lambda _w, f=cb: f())
            grade.attach(b, i % 4, i // 4, 1, 1)
        c.append(grade)
        raiz.append(c)

        c2 = self._cartao("pendências")
        self.l_pend = Gtk.Label()
        self.l_pend.set_xalign(0)
        self.l_pend.set_wrap(True)
        self.l_pend.add_css_class("sub")
        c2.append(self.l_pend)
        raiz.append(c2)
        return raiz

    # -------------------------------------------------------------- FASE 2
    def _pagina_fase2(self) -> Gtk.Widget:
        raiz = self._pagina_base()
        c = self._cartao("kit da fase 2 — carla como rack na sessão modelo")
        for chave, attr in (("sessão modelo", "l_f2_sess"), ("rack (.carxp)", "l_f2_rack"),
                            ("conteúdo do rack", "l_f2_doc"), ("estado", "l_f2_estado")):
            linha, v = self._campo(chave)
            setattr(self, attr, v)
            c.append(linha)

        grade = Gtk.Grid(column_spacing=8, row_spacing=8)
        itens = [("Preparar sessão + rack", self._f2_preparar),
                 ("Validar rack", self._f2_validar),
                 ("Plano do patch (leitura)", self._f2_plano),
                 ("Abrir Carla · rack", self._f2_abrir_carla),
                 ("Abrir sessão modelo", self._f2_abrir_ardour),
                 ("Recarregar", self._f2_refresh)]
        for i, (rotulo, cb) in enumerate(itens):
            b = Gtk.Button(label=rotulo)
            b.connect("clicked", lambda _w, f=cb: f())
            grade.attach(b, i % 3, i // 3, 1, 1)
        c.append(grade)
        raiz.append(c)

        c2 = self._cartao("plano de conexões (só leitura — nada é conectado)")
        sw = Gtk.ScrolledWindow()
        sw.set_min_content_height(240)
        self.txt_f2 = Gtk.TextView()
        self.txt_f2.set_monospace(True)
        self.txt_f2.set_editable(False)
        self.txt_f2.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        sw.set_child(self.txt_f2)
        c2.append(sw)
        raiz.append(c2)

        self._f2_refresh()
        return raiz

    def _f2_refresh(self) -> None:
        """Olha o disco (sem executar nada) e mostra o estado do kit."""
        self.f2 = fase2.preparar_dry()
        info = self.f2
        self._definir(self.l_f2_sess, f"{info['sessao']}" if info["sessao"] else "—",
                      "valor" if info["sessao"] else "off")
        self._definir(self.l_f2_rack, info["rack"].name if info["rack"] else "—",
                      "valor" if info["rack"] else "off")
        if info["rack"]:
            ok, detalhe = projeto.validar(info["rack"])
            self._definir(self.l_f2_doc, detalhe if ok else f"inválido: {detalhe}",
                          "sub" if ok else "erro")
        else:
            self._definir(self.l_f2_doc, "rack ainda não gerado", "off")
        self._definir(self.l_f2_estado, info["estado"], "aviso" if not info["pronto"] else "valor")

    def _f2_preparar(self, *_):
        try:
            info = fase2.preparar()
        except Exception as e:
            self._log(f"fase 2: erro preparando: {e}")
            return
        for a in info["avisos"]:
            self._log(f"fase 2: {a}")
        self._f2_refresh()

    def _f2_validar(self, *_):
        rack = getattr(self, "f2", {}).get("rack")
        if not rack:
            self._log("fase 2: rack ainda não existe (clique em Preparar)")
            return
        ok, detalhe = projeto.validar(rack)
        self._log(f"fase 2: {rack.name} — {'ok' if ok else 'PROBLEMA'}: {detalhe}")
        self._f2_refresh()

    def _f2_plano(self, *_):
        """Mostra o plano lendo o grafo do PipeWire. NUNCA conecta nada aqui."""
        try:
            passos = fase2.plano()
        except Exception as e:
            self._log(f"fase 2: erro lendo o grafo: {e}")
            return
        linhas = ["plano do patch (nada é executado por esta janela)", ""]
        for p in passos:
            linhas.append(f"{p['rotulo']}")
            linhas.append(f"  {p['motivo']}")
            for saida, entrada in p["conexoes"]:
                linhas.append(f"    pw-link {saida}  {entrada}")
            if not p["conexoes"]:
                linhas.append("    (nada a fazer)")
            linhas.append("")
        self.txt_f2.get_buffer().set_text("\n".join(linhas))
        self._log("fase 2: plano do patch atualizado (só leitura)")

    def _f2_abrir_carla(self, *_):
        rack = getattr(self, "f2", {}).get("rack")
        if not rack:
            self._log("fase 2: gere o rack primeiro (Preparar sessão + rack)")
            return
        try:
            proc, log = carla.launch("rack", rack)
            self._log(f"Carla rack aberto com {rack.name} (pid {proc.pid}) — log {log}")
        except Exception as e:
            self._log(f"erro abrindo Carla rack: {e}")

    def _f2_abrir_ardour(self, *_):
        sess = getattr(self, "f2", {}).get("sessao")
        if not sess:
            self._log("fase 2: gere a sessão modelo primeiro")
            return
        try:
            proc, log = ardour.launch(sessao=Path(sess), new=False, backend="jack")
            self._log(f"Ardour abriu a sessão modelo (pid {proc.pid}) — log {log}")
        except Exception as e:
            self._log(f"erro abrindo o Ardour: {e}")

    def _abrir(self, exe: str, extra: list[str] | None = None) -> None:
        if not shutil.which(exe):
            self._log(f"{exe}: não instalado")
            return
        try:
            subprocess.Popen([exe, *(extra or [])], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                             start_new_session=True)
            self._log(f"aberto: {exe} {' '.join(extra or [])}".strip())
        except OSError as e:
            self._log(f"erro abrindo {exe}: {e}")

    def _abrir_carla(self, modo: str) -> None:
        try:
            proc, log = carla.launch(modo)
            self._log(f"Carla {modo} aberto (pid {proc.pid}) — log {log}")
        except Exception as e:
            self._log(f"erro abrindo Carla {modo}: {e}")

    # -------------------------------------------------------------- ações Ardour
    def _backend(self) -> str:
        return ardour.BACKENDS[self.dd_backend.get_selected()]

    # -------- sessões / motor / OSC (RE 03) -----------------------------
    def _refresh_sessoes(self, *_):
        """(Re)lista as sessoes; mexe na UI so quando a lista muda de verdade.

        Nao chama _on_sessao(): isso sobrescreveria o nome que o usuario digitou.
        """
        novas = ardour.sessions()
        nomes = [p.name for p in novas] or ["—"]
        if nomes != self._nomes_sess:
            self._sessoes = novas
            self._nomes_sess = nomes
            self.dd_sess.set_model(Gtk.StringList.new(nomes))
        self._mostra_motor()

    def _sessao_escolhida(self):
        if not self._sessoes:
            return None
        i = self.dd_sess.get_selected()
        if i < 0 or i >= len(self._sessoes):
            return None
        return self._sessoes[i]

    def _on_sessao(self, *_):
        s = self._sessao_escolhida()
        if s is not None:
            self.en_sessao.set_text(s.name)
        self._mostra_motor()

    def _mostra_motor(self):
        s = self._sessao_escolhida()
        if s is None:
            self.l_motor_sess.set_text("nenhuma sessão do Ardour ainda")
            return
        h = ardour.engine_hints(s)
        b = h.get("backend", "?")
        extra = ""
        if b == "ALSA" and h.get("output-device"):
            extra = f'  ({h.get("input-device", "?")} → {h.get("output-device")})'
        self.l_motor_sess.set_text(f"{b}{extra}")
        loc = ardour.BACKEND_LOCAL.get(b.lower())
        if loc in ardour.BACKENDS:
            self.dd_backend.set_selected(ardour.BACKENDS.index(loc))

    def _gravar_motor(self, *_):
        s = self._sessao_escolhida()
        if s is None:
            self._log("gravar motor: nenhuma sessão selecionada.")
            return
        if ardour.is_running():
            self._log("gravar motor: feche o Ardour antes (ele regrava a sessão ao sair).")
            return
        ok, msg = ardour.definir_engine(s, self._backend())
        self._log(("motor: " if ok else "ERRO motor: ") + msg)
        self._on_sessao()

    def _ligar_osc(self, *_):
        if ardour.is_running():
            self._log("OSC: feche o Ardour antes (ele regrava o config ao sair).")
            return
        ok, msg = ardour.habilitar_osc(True)
        self._log(("OSC: " if ok else "ERRO OSC: ") + msg)
        self._atualiza_status()

    def _abrir_sessao(self, *_):
        s = self._sessao_escolhida()
        nome = self.en_sessao.get_text().strip()
        alvo = s if (s is not None and s.name == nome) else None
        try:
            proc, log = ardour.launch(alvo.name if alvo else (nome or None),
                                      new=False, backend=self._backend(), sessao=alvo)
            self._log(f"Ardour aberto (pid {proc.pid}) motor={self._backend()} log={log}")
        except Exception as e:
            self._log(f"ERRO abrindo Ardour: {e}")

    def _nova_sessao(self, *_):
        nome = self.en_sessao.get_text().strip() or "sessao-spook"
        self.en_sessao.set_text(nome)
        try:
            proc, log = ardour.launch(nome, new=True, backend=self._backend())
            self._log(f"nova sessão '{nome}' (pid {proc.pid}) motor={self._backend()} log={log}")
        except Exception as e:
            self._log(f"ERRO criando sessão: {e}")

    def _parar_ardour(self, *_):
        pids = ardour.running()
        if not pids:
            self._log("Ardour já está parado.")
            return
        ardour.stop()
        self._log(f"Ardour encerrado (pids {', '.join(map(str, pids))})")

    def _testar_osc(self, *_):
        if self.trans.heartbeat():
            self._log(f"OSC ok — o Ardour respondeu ({self.trans.resposta()}).")
        elif ardour.is_running():
            self._log("Ardour rodando mas sem resposta OSC — ligue a surface "
                      "'Open Sound Control (OSC)' (Preferences > Control Surfaces).")
        else:
            self._log("Ardour não está rodando — o OSC não tem com quem falar.")

    def _enviar(self, acao: str) -> None:
        addr = self.trans.send(acao)
        self._log(f"OSC → {acao}  ({addr})")

    def _osc_feedback(self, addr: str, args: list) -> None:
        """Chamado pela thread do OSC (nao toca em GTK — so guarda)."""
        self.fb[addr] = args

    # -------------------------------------------------------------- status
    def _atualiza_status(self):
        st = pw.services()
        ok = st.get("pipewire") == "active"
        self._definir(self.l_pw, f'pipewire {st.get("pipewire")} · pulse {st.get("pipewire-pulse")} · '
                                 f'wireplumber {st.get("wireplumber")}', "valor" if ok else "erro")
        j = pw.jack_status()
        self._definir(self.l_pwj, f'{j["portas"]} portas ({j["detalhe"]})',
                      "valor" if j["disponivel"] else "aviso")
        clk = pw.clock()
        self._definir(self.l_clk, f'{clk.get("clock.rate", "?")} Hz · quantum {clk.get("clock.quantum", "?")}')
        s = pw.snapshot()
        self._definir(self.l_sink, pw.default_sink(s) or "—")
        self._definir(self.l_src, pw.default_source(s) or "—")
        self._definir(self.l_ard, f"{ardour.which() or '—'} · {ardour.version() or '?'}")
        pids = ardour.running()
        self._definir(self.l_ardst, f'rodando (pid {",".join(map(str, pids))})' if pids else "parado",
                      "valor" if pids else "sub")
        if getattr(self, "l_osc", None) is not None:
            st_osc = ardour.osc_ativo()
            self._definir(self.l_osc, ardour.osc_ativo_texto(),
                          "valor" if st_osc else "aviso")
            self._refresh_sessoes()
        if getattr(self, "l_fb", None) is not None:
            interessantes = ("/session_name", "/record_enabled", "/is_recording", "/group/list")
            partes = [f"{a.lstrip('/')}={', '.join(map(str, self.fb[a]))}"
                      for a in interessantes if a in self.fb]
            self.l_fb.set_text(("feedback do Ardour: " + " · ".join(partes)) if partes else
                               "feedback do Ardour: nada ainda (só chega com o Ardour "
                               "rodando e o OSC ligado)")
        faltando = [e for e in ("ardour9", "alsa-scarlett-gui", "carla-rack", "qpwgraph", "helvum",
                                "pavucontrol", "qjackctl", "alsactl", "jack_lsp") if not shutil.which(e)]
        self.lbl_rodape.set_markup(f'SPOOK AUDIO 0.1 · {len(s.nodes)} nós · '
                                   f'{len(s.links)} conexões' +
                                   (f' · ausentes: {", ".join(faltando)}' if faltando else ''))
        if getattr(self, "l_pend", None) is not None:
            self.l_pend.set_text("ferramentas ausentes: " + ", ".join(faltando) if faltando
                                 else "tudo o que o painel usa está instalado.")

    def _tick(self):
        self._atualiza_status()
        return True


def main(argv: list[str] | None = None) -> int:
    app = Gtk.Application(application_id="org.spook.Audio")

    def _activate(a):
        w = Janela(a)
        w.present()

    app.connect("activate", _activate)
    return app.run(argv if argv is not None else [])
